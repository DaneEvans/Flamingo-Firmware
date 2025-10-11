#include "TextMessageModule.h"
#include "MeshService.h"
#include "MeshTypes.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "buzz.h"
#include "configuration.h"
#include "RangeTestModule.h"

#include "graphics/Screen.h"
TextMessageModule *textMessageModule;

#define MAX_ADMIN_MSG 31

void parseAdmin(pb_size_t size, char* payload){
    char local_payload[MAX_ADMIN_MSG+1];
    pb_size_t new_size;
    if (size < 4) return;  // too short to be an ADMIN message

    // Check for Alert Bell emojii, toggle RT if alert bell received
    if (payload[0] == 0xF0 && payload[1] == 0x9F && payload[2] == 0x94 && payload[3] == 0x94 ) {
        if (getRtDynanmicEnable()) {
            LOG_INFO("Found Alert Bell, Dynamic Rangetest OFF ");
            setRtDynamicEnable(0);
        } else {
            LOG_INFO("Found Alert Bell, Dynamic Rangetest ON ");
            setRtDynamicEnable(1);
        }
        return;
    }

    if (!( (payload[0] == 'A' || payload[0] == 'a') && (payload[1] == 'D' || payload[1] == 'd'))) return;
    // this is an ADMIN message
    new_size = (size < MAX_ADMIN_MSG) ? size : MAX_ADMIN_MSG;
    strncpy(local_payload, payload, new_size);
    local_payload[new_size] = '\0';

    for (int i = 0; i < new_size; i++) {
        local_payload[i] = tolower(local_payload[i]);
    }


    if (strcmp("adrt on hop", local_payload) == 0) {
        LOG_INFO("Turning Dynamic Rangetest ON with hop");
        setRtDynamicEnable(1);
        setRtHop(1);
    }
    else if (strcmp("adrt on", local_payload) == 0) {
        LOG_INFO("Turning Dynamic Rangetest ON");
        setRtDynamicEnable(1);
        setRtHop(0);
    }
    else if (strcmp("adrt off", local_payload) == 0) {
        LOG_INFO("Turning Dynamic Rangetest OFF");
        setRtDynamicEnable(0);
    }
    else if (strncmp("adrt delay", local_payload, 10) == 0 && new_size >= 13) {
        if (strncmp("15", local_payload+11, 2) == 0) {
            LOG_INFO("Rangetest delay is 15");
            moduleConfig.range_test.sender = 15;
        } 
        else if (strncmp("30", local_payload+11, 2) == 0) {
            LOG_INFO("Rangetest delay is 30");
            moduleConfig.range_test.sender = 30;
        }
        else if (strncmp("60", local_payload+11, 2) == 0) {
            LOG_INFO("Rangetest delay is 60");
            moduleConfig.range_test.sender = 60;
        } 
    }

}

#ifdef DEBUG_PORT
char textmsg[201];
#endif


ProcessMessage TextMessageModule::handleReceived(const meshtastic_MeshPacket &mp)
{
#ifdef DEBUG_PORT
    auto rssi = mp.rx_rssi;
    auto &p = mp.decoded;
    //LOG_INFO("Received text msg from=0x%0x, id=0x%x, msg=%.*s", mp.from, mp.id, p.payload.size, p.payload.bytes);
    meshtastic_NodeInfoLite *n = nodeDB->getMeshNode(getFrom(&mp));
    /*
    LOG_INFO("TextModule msg: from=0x%0x, id=0x%x, ln=%s, rxSNR=%g, hop_limit=%d, hop_start=%d, msg=%.*s",
        mp.from, mp.id, n->user.long_name, mp.rx_snr, mp.hop_limit, mp.hop_start, p.payload.size, p.payload.bytes);
    */

    LOG_INFO("TextModule msg: from=0x%0x, id=0x%x, ln=%s, rxSNR=%g, hop_limit=%d, hop_start=%d",
        mp.from, mp.id, n->user.long_name, mp.rx_snr, mp.hop_limit, mp.hop_start);
    uint16_t offset;
    uint16_t bytes_left = p.payload.size;
    bool do_loop = 1;
    offset = 0;
    /* apparently, the maximum size log message is about 150 characters. Deal this with this*/
    while (do_loop) {
        if (bytes_left <= 150) {
            memset(textmsg, 0, sizeof(textmsg));
            strncpy(textmsg, (char *)(p.payload.bytes+offset), bytes_left);
            do_loop = 0;
        } else {
            memset(textmsg, 0, sizeof(textmsg));
            strncpy(textmsg, (char *)(p.payload.bytes+offset), 150);
            offset = offset + 150;
            bytes_left = bytes_left-150;
        }
        LOG_INFO("z=%s",textmsg);
    }
    if (!isBroadcast(mp.to)) {
        // Direct message, check if admin
        parseAdmin(p.payload.size, (char *)p.payload.bytes);
    }


#endif

    // We only store/display messages destined for us.
    // Keep a copy of the most recent text message.
    devicestate.rx_text_message = mp;
    devicestate.has_rx_text_message = true;

    // Only trigger screen wake if configuration allows it
    if (shouldWakeOnReceivedMessage()) {
        powerFSM.trigger(EVENT_RECEIVED_MSG);
    }
    notifyObservers(&mp);

    return ProcessMessage::CONTINUE; // Let others look at this message also if they want
}

bool TextMessageModule::wantPacket(const meshtastic_MeshPacket *p)
{
    return MeshService::isTextPayload(p);
}
