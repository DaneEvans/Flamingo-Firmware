#pragma once

#include "Router.h"

#ifdef FLAMINGO_MAX_REXMIT
/**
 * An identifier for a globally unique message - a pair of the sending nodenum and the packet id assigned
 * to that message
 */
struct GlobalPacketId {
    NodeNum node;
    PacketId id;

    bool operator==(const GlobalPacketId &p) const { return node == p.node && id == p.id; }

    explicit GlobalPacketId(const meshtastic_MeshPacket *p)
    {
        node = getFrom(p);
        id = p->id;
    }

    GlobalPacketId(NodeNum _from, PacketId _id)
    {
        node = _from;
        id = _id;
    }
};

/**
 * A packet queued for retransmission
 */
struct PendingPacket {
    meshtastic_MeshPacket *packet;

    /** The next time we should try to retransmit this packet */
    uint32_t nextTxMsec = 0;

    /** Starts at NUM_RETRANSMISSIONS -1 and counts down.  Once zero it will be removed from the list */
    uint8_t numRetransmissions = 0;

    PendingPacket() {}
    explicit PendingPacket(meshtastic_MeshPacket *p, uint8_t numRetransmissions);
};

class GlobalPacketIdHashFunction
{
  public:
    size_t operator()(const GlobalPacketId &p) const { return (std::hash<NodeNum>()(p.node)) ^ (std::hash<PacketId>()(p.id)); }
};

#endif

/**
 * This is a mixin that extends Router with the ability to do Naive Flooding (in the standard mesh protocol sense)
 *
 *   Rules for broadcasting (listing here for now, will move elsewhere eventually):

  If to==BROADCAST and id==0, this is a simple broadcast (0 hops).  It will be
  sent only by the current node and other nodes will not attempt to rebroadcast
  it.

  If to==BROADCAST and id!=0, this is a "naive flooding" broadcast.  The initial
  node will send it on all local interfaces.

  When other nodes receive this message, they will
  first check if their recentBroadcasts table contains the (from, id) pair that
  indicates this message.  If so, we've already seen it - so we discard it.  If
  not, we add it to the table and then resend this message on all interfaces.
  When resending we are careful to use the "from" ID of the original sender. Not
  our own ID.  When resending we pick a random delay between 0 and 10 seconds to
  decrease the chance of collisions with transmitters we can not even hear.

  Any entries in recentBroadcasts that are older than X seconds (longer than the
  max time a flood can take) will be discarded.
 */
class FloodingRouter : public Router
{
  public:
    /**
     * Constructor
     *
     */
    FloodingRouter();

    /**
     * Send a packet on a suitable interface.  This routine will
     * later free() the packet to pool.  This routine is not allowed to stall.
     * If the txmit queue is full it might return an error
     */
    virtual ErrorCode send(meshtastic_MeshPacket *p) override;

#ifdef FLAMINGO_MAX_REXMIT
    /** Do our retransmission handling */
    virtual int32_t runOnce() override
    {
        // Note: We must doRetransmissions FIRST, because it might queue up work for the base class runOnce implementation
        doRetransmissions();

        int32_t r = Router::runOnce();

        // Also after calling runOnce there might be new packets to retransmit
        auto d = doRetransmissions();
        return min(d, r);
    }


    constexpr static uint8_t NUM_INTERMEDIATE_RETX = FLAMINGO_MAX_REXMIT+1;
    // The number of retransmissions the original sender will do
    constexpr static uint8_t NUM_RELIABLE_RETX = 3;

  private:
    /* Check if we should rebroadcast this packet, and do so if needed */
    void perhapsRebroadcast(const meshtastic_MeshPacket *p);
#endif

  protected:
    /**
     * Should this incoming filter be dropped?
     *
     * Called immediately on reception, before any further processing.
     * @return true to abandon the packet
     */
    virtual bool shouldFilterReceived(const meshtastic_MeshPacket *p) override;

    /**
     * Look for broadcasts we need to rebroadcast
     */
    virtual void sniffReceived(const meshtastic_MeshPacket *p, const meshtastic_Routing *c) override;

#ifndef FLAMINGO_MAX_REXMIT
    /* Check if we should rebroadcast this packet, and do so if needed */
    virtual bool perhapsRebroadcast(const meshtastic_MeshPacket *p) = 0;
#endif

    /* Check if we should handle an upgraded packet (with higher hop_limit)
     * @return true if we handled it (so stop processing)
     */
    bool perhapsHandleUpgradedPacket(const meshtastic_MeshPacket *p);

    /* Call when we receive a packet that needs some reprocessing, but afterwards should be filtered */
    void reprocessPacket(const meshtastic_MeshPacket *p);

    // Return false for roles like ROUTER which should always rebroadcast even when we've heard another rebroadcast of
    // the same packet
    bool roleAllowsCancelingDupe(const meshtastic_MeshPacket *p);

    /* Call when receiving a duplicate packet to check whether we should cancel a packet in the Tx queue */
    void perhapsCancelDupe(const meshtastic_MeshPacket *p);

    // Return true if we are a rebroadcaster
    bool isRebroadcaster();
#ifdef FLAMINGO_MAX_REXMIT

    /**
     * Pending retransmissions
     */
    std::unordered_map<GlobalPacketId, PendingPacket, GlobalPacketIdHashFunction> pending;

    /**
     * Try to find the pending packet record for this ID (or NULL if not found)
     */
    PendingPacket *findPendingPacket(NodeNum from, PacketId id) { return findPendingPacket(GlobalPacketId(from, id)); }
    PendingPacket *findPendingPacket(GlobalPacketId p);

    /**
     * Add p to the list of packets to retransmit occasionally.  We will free it once we stop retransmitting.
     */
    PendingPacket *startRetransmission(meshtastic_MeshPacket *p, uint8_t numReTx = NUM_INTERMEDIATE_RETX);

    /**
     * Stop any retransmissions we are doing of the specified node/packet ID pair
     *
     * @return true if we found and removed a transmission with this ID
     */
    bool stopRetransmission(NodeNum from, PacketId id);
    bool stopRetransmission(GlobalPacketId p);
    /**
     * Do any retransmissions that are scheduled (FIXME - for the time being called from loop)
     *
     * @return the number of msecs until our next retransmission or MAXINT if none scheduled
     */
    int32_t doRetransmissions();

    void setNextTx(PendingPacket *pending);
#endif
};