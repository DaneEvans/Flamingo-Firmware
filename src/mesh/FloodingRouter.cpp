#include "FloodingRouter.h"

#include "configuration.h"
#include "mesh-pb-constants.h"

FloodingRouter::FloodingRouter() {}

/**
 * Send a packet on a suitable interface.  This routine will
 * later free() the packet to pool.  This routine is not allowed to stall.
 * If the txmit queue is full it might return an error
 */
ErrorCode FloodingRouter::send(meshtastic_MeshPacket *p)
{
    // Add any messages _we_ send to the seen message list (so we will ignore all retransmissions we see)
    p->relay_node = nodeDB->getLastByteOfNodeNum(getNodeNum()); // First set the relayer to us
    wasSeenRecently(p);                                         // FIXME, move this to a sniffSent method

    return Router::send(p);
}

bool FloodingRouter::shouldFilterReceived(const meshtastic_MeshPacket *p)
{
    if (wasSeenRecently(p)) { // Note: this will also add a recent packet record
        printPacket("Ignore dupe incoming msg", p);
        rxDupe++;

        /* If the original transmitter is doing retransmissions (hopStart equals hopLimit) for a reliable transmission, e.g., when
        the ACK got lost, we will handle the packet again to make sure it gets an implicit ACK. */
        bool isRepeated = p->hop_start > 0 && p->hop_start == p->hop_limit;
        if (isRepeated) {
            LOG_DEBUG("Repeated reliable tx");
            // Check if it's still in the Tx queue, if not, we have to relay it again
            if (!findInTxQueue(p->from, p->id))
                perhapsRebroadcast(p);
        } else {
            perhapsCancelDupe(p);
        }

        return true;
    }

    return Router::shouldFilterReceived(p);
}

void FloodingRouter::perhapsCancelDupe(const meshtastic_MeshPacket *p)
{
    if (config.device.role != meshtastic_Config_DeviceConfig_Role_ROUTER &&
        config.device.role != meshtastic_Config_DeviceConfig_Role_REPEATER &&
        config.device.role != meshtastic_Config_DeviceConfig_Role_ROUTER_LATE &&
        p->transport_mechanism == meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA) {
        // cancel rebroadcast of this message *if* there was already one, unless we're a router/repeater!
        // But only LoRa packets should be able to trigger this.
        if (Router::cancelSending(p->from, p->id))
            txRelayCanceled++;
    }
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER_LATE && iface) {
        iface->clampToLateRebroadcastWindow(getFrom(p), p->id);
    }
}

bool FloodingRouter::isRebroadcaster()
{
    return config.device.role != meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE &&
           config.device.rebroadcast_mode != meshtastic_Config_DeviceConfig_RebroadcastMode_NONE;
}

void FloodingRouter::perhapsRebroadcast(const meshtastic_MeshPacket *p)
{
    if (!isToUs(p) && (p->hop_limit > 0) && !isFromUs(p)) {
        if (p->id != 0) {
            if (isRebroadcaster()) {
                meshtastic_MeshPacket *tosend = packetPool.allocCopy(*p); // keep a copy because we will be sending it

                tosend->hop_limit--; // bump down the hop count
#if USERPREFS_EVENT_MODE
                if (tosend->hop_limit > 2) {
                    // if we are "correcting" the hop_limit, "correct" the hop_start by the same amount to preserve hops away.
                    tosend->hop_start -= (tosend->hop_limit - 2);
                    tosend->hop_limit = 2;
                }
#endif
                tosend->next_hop = NO_NEXT_HOP_PREFERENCE; // this should already be the case, but just in case

                LOG_INFO("Rebroadcast received floodmsg");
#ifdef FLAMINGO_MAX_REXMIT
                // this method gets handed packets that have been seen recently and is a reliable router repeat
                // do not add these to the retransmit queue, causes an infinite loop
                bool isRepeated = p->hop_start > 0 && p->hop_start == p->hop_limit;
                if (FLAMINGO_MAX_REXMIT > 0 && !isRepeated) {
                    if ((!isFromUs(p) || !p->want_ack) &&  (p->hop_limit > 0 || p->want_ack)) {
                        startRetransmission(packetPool.allocCopy(*p)); // start retransmission for relayed packet
                    }
                }
#endif
                // Note: we are careful to resend using the original senders node id
                // We are careful not to call our hooked version of send() - because we don't want to check this again
                Router::send(tosend);
            } else {
                LOG_DEBUG("No rebroadcast: Role = CLIENT_MUTE or Rebroadcast Mode = NONE");
            }
        } else {
            LOG_DEBUG("Ignore 0 id broadcast");
        }
    }
}

void FloodingRouter::sniffReceived(const meshtastic_MeshPacket *p, const meshtastic_Routing *c)
{
    bool isAckorReply = (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag) &&
                        (p->decoded.request_id != 0 || p->decoded.reply_id != 0);
    if (isAckorReply && !isToUs(p) && !isBroadcast(p->to)) {
        // do not flood direct message that is ACKed or replied to
        LOG_DEBUG("Rxd an ACK/reply not for me, cancel rebroadcast");
        Router::cancelSending(p->to, p->decoded.request_id); // cancel rebroadcast for this DM
    }

    perhapsRebroadcast(p);

    // handle the packet as normal
    Router::sniffReceived(p, c);
}

#ifdef FLAMINGO_MAX_REXMIT
PendingPacket *FloodingRouter::startRetransmission(meshtastic_MeshPacket *p, uint8_t numReTx)
{

    auto id = GlobalPacketId(p);
    auto rec = PendingPacket(p, numReTx);
    LOG_DEBUG("FloodRtr::startRetran fr=0x%x,to=0x%x,id=0x%x, tries left=%d", p->from, p->to,
                          p->id, numReTx);
    stopRetransmission(getFrom(p), p->id);
    setNextTx(&rec);
    pending[id] = rec;

    return &pending[id];

}

PendingPacket *FloodingRouter::findPendingPacket(GlobalPacketId key)
{
    auto old = pending.find(key); // If we have an old record, someone messed up because id got reused
    if (old != pending.end()) {
        return &old->second;
    } else
        return NULL;
}

/**
 * Stop any retransmissions we are doing of the specified node/packet ID pair
 */
bool FloodingRouter::stopRetransmission(NodeNum from, PacketId id)
{
    auto key = GlobalPacketId(from, id);
    return stopRetransmission(key);
}

bool FloodingRouter::stopRetransmission(GlobalPacketId key)
{
    auto old = findPendingPacket(key);
    if (old) {
        auto p = old->packet;
        /* Only when we already transmitted a packet via LoRa, we will cancel the packet in the Tx queue
          to avoid canceling a transmission if it was ACKed super fast via MQTT */
        if (old->numRetransmissions < NUM_RELIABLE_RETX - 1) {
            // We only cancel it if we are the original sender or if we're not a router(_late)/repeater
            if (isFromUs(p) || (config.device.role != meshtastic_Config_DeviceConfig_Role_ROUTER &&
                                config.device.role != meshtastic_Config_DeviceConfig_Role_REPEATER &&
                                config.device.role != meshtastic_Config_DeviceConfig_Role_ROUTER_LATE)) {
                // remove the 'original' (identified by originator and packet->id) from the txqueue and free it
                cancelSending(getFrom(p), p->id);
            }
        }

        // Regardless of whether or not we canceled this packet from the txQueue, remove it from our pending list so it doesn't
        // get scheduled again. (This is the core of stopRetransmission.)
        auto numErased = pending.erase(key);
        assert(numErased == 1);

        // When we remove an entry from pending, always be sure to release the copy of the packet that was allocated in the call
        // to startRetransmission.
        packetPool.release(p);

        return true;
    } else
        return false;
}

/**
 * Do any retransmissions that are scheduled (FIXME - for the time being called from loop)
 */
int32_t FloodingRouter::doRetransmissions()
{
    uint32_t now = millis();
    int32_t d = INT32_MAX;
    LOG_DEBUG("In FloodingRouter:doRetran");

    // FIXME, we should use a better datastructure rather than walking through this map.
    // for(auto el: pending) {
    for (auto it = pending.begin(), nextIt = it; it != pending.end(); it = nextIt) {
        ++nextIt; // we use this odd pattern because we might be deleting it...
        auto &p = it->second;

        bool stillValid = true; // assume we'll keep this record around

        // FIXME, handle 51 day rolloever here!!!
        if (p.nextTxMsec <= now) {
            if (p.numRetransmissions == 0) {
                // Note: we don't stop retransmission here, instead the Nak packet gets processed in sniffReceived
                stopRetransmission(it->first);
                stillValid = false; // just deleted it
            } else {
                LOG_DEBUG("Sending retransmission fr=0x%x,to=0x%x,id=0x%x, tries left=%d", p.packet->from, p.packet->to,
                          p.packet->id, p.numRetransmissions);
                Router::send(packetPool.allocCopy(*p.packet));
                // Queue again
                --p.numRetransmissions;
                setNextTx(&p);
            }
        }

        if (stillValid) {
            // Update our desired sleep delay
            int32_t t = p.nextTxMsec - now;

            d = min(t, d);
        }
    }

    return d;
}


void FloodingRouter::setNextTx(PendingPacket *pending)
{
    assert(iface);
    auto d = iface->getRetransmissionMsec(pending->packet);
    pending->nextTxMsec = millis() + d;
    LOG_DEBUG("Setting next retransmission in %u msecs: ", d);
    setReceivedMessage(); // Run ASAP, so we can figure out our correct sleep time
}


#endif
