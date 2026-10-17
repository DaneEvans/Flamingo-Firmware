#include "TextMessageModule.h"
#include "MeshService.h"
#include "MessageStore.h"
#ifdef FLAMINGO
#include "MeshTypes.h"
#endif
#include "FSCommon.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "SPILock.h"
#include "buzz.h"
#include "configuration.h"
#ifdef FLAMINGO
#include "RangeTestModule.h"
#ifdef FLAMINGO_CONNECTION_LED
#include "BlinkModule.h"
#endif
#endif

#include "graphics/Screen.h"
#include "graphics/SharedUIDisplay.h"
#include "graphics/draw/MessageRenderer.h"
#include "main.h"
TextMessageModule *textMessageModule;

#ifdef FLAMINGO
static void loadAutoReplyState();
#endif

#ifdef FLAMINGO
TextMessageModule::TextMessageModule() : SinglePortModule("text", meshtastic_PortNum_TEXT_MESSAGE_APP)
{
    loadAutoReplyState();
}
#else
TextMessageModule::TextMessageModule() : SinglePortModule("text", meshtastic_PortNum_TEXT_MESSAGE_APP) {}
#endif

#ifdef FLAMINGO
#define MAX_ADMIN_MSG 31
#define AUTO_REPLY_PERSIST_PATH "/prefs/adar.proto"

static bool autoReplyEnabled = false;

enum class AutoReplyMode : uint8_t {
    ALL = 0,
    PRIM = 1,
    PRIM_DM = 2,
    SEC = 3,
    SEC_DM = 4,
};

static AutoReplyMode autoReplyMode = AutoReplyMode::ALL;

static const char *autoReplyModeName(AutoReplyMode mode)
{
    switch (mode) {
    case AutoReplyMode::ALL:
        return "ALL";
    case AutoReplyMode::PRIM:
        return "PRIM";
    case AutoReplyMode::PRIM_DM:
        return "PRIM+DM";
    case AutoReplyMode::SEC:
        return "SEC";
    case AutoReplyMode::SEC_DM:
        return "SEC+DM";
    default:
        return "ALL";
    }
}

static void sendAutoReplyState(NodeNum to)
{
    if (to == 0)
        return;

    meshtastic_MeshPacket *p = router->allocForSending();
    p->to = to;
    p->channel = 0;
    p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    p->want_ack = false;
    p->decoded.want_response = false;

    char msg[31];
    snprintf(msg, sizeof(msg), "ADAR %s %s", autoReplyEnabled ? "ON" : "OFF", autoReplyModeName(autoReplyMode));
    p->decoded.payload.size = strlen(msg);
    memcpy(p->decoded.payload.bytes, msg, p->decoded.payload.size);
    service->sendToMesh(p);
}

static void sendAdminHelp(NodeNum to)
{
    if (to == 0)
        return;

    static const char *helpLines[] = {
        "AD HELP",
        "ADRT: ON|OFF|ON HOP|DELAY 15/30/60",
        "ADAR: ON|OFF|TOGGLE|ALL|PRIM|PRIM+DM|SEC|SEC+DM",
        "ADAR (no arg): show current state",
#ifdef FLAMINGO_CONNECTION_LED
        "ADLED: <color> [timeout] | DEFAULT <color>",
#endif
    };

    for (const char *line : helpLines) {
        meshtastic_MeshPacket *p = router->allocForSending();
        p->to = to;
        p->channel = 0;
        p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
        p->want_ack = false;
        p->decoded.want_response = false;
        p->decoded.payload.size = strlen(line);
        memcpy(p->decoded.payload.bytes, line, p->decoded.payload.size);
        service->sendToMesh(p);
    }
}

static void loadAutoReplyState()
{
    if (!FSCom.exists(AUTO_REPLY_PERSIST_PATH)) {
        return;
    }

    File f = FSCom.open(AUTO_REPLY_PERSIST_PATH, FILE_O_READ);
    if (!f) {
        return;
    }

    uint8_t buffer[8] = {0};
    size_t readSize = f.read(buffer, sizeof(buffer));
    if (readSize > 0) {
        autoReplyEnabled = buffer[0] != 0;
        if (readSize > 1 && buffer[1] <= static_cast<uint8_t>(AutoReplyMode::SEC_DM)) {
            autoReplyMode = static_cast<AutoReplyMode>(buffer[1]);
        }
    }
    f.close();
}

static void saveAutoReplyState()
{
#ifdef FSCom
    spiLock->lock();
    FSCom.mkdir("/prefs");
    spiLock->unlock();
#endif

    File f = FSCom.open(AUTO_REPLY_PERSIST_PATH, FILE_O_WRITE);
    if (f) {
        uint8_t buffer[8] = {0};
        buffer[0] = autoReplyEnabled ? 1 : 0;
        buffer[1] = static_cast<uint8_t>(autoReplyMode);
        f.write(buffer, sizeof(buffer));
        f.close();
    }
}

static void sendAutoReply(uint32_t from, bool wasBroadcast, uint8_t channel, uint32_t replyId)
{
    if (!autoReplyEnabled || from == 0 || from == nodeDB->getNodeNum()) {
        return;
    }
    meshtastic_MeshPacket *p = router->allocForSending();
    p->to = wasBroadcast ? NODENUM_BROADCAST : from;
    p->channel = channel;
    p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    p->want_ack = false;
    p->decoded.want_response = false;
    p->decoded.emoji = 1;
    p->decoded.reply_id = replyId;
    static constexpr char robotReply[] = "🤖";
    p->decoded.payload.size = strlen(robotReply);
    memcpy(p->decoded.payload.bytes, robotReply, p->decoded.payload.size);
    service->sendToMesh(p);
}

void parseAdmin(pb_size_t size, char *payload, bool isToUs, NodeNum from)
{
    char local_payload[MAX_ADMIN_MSG + 1];
    pb_size_t new_size;
    if (size < 4)
        return; // too short to be an ADMIN message

    // Check for Alert Bell emojii, toggle RT if alert bell received
    if (payload[0] == 0xF0 && payload[1] == 0x9F && payload[2] == 0x94 && payload[3] == 0x94) {
        if (!isToUs) {
            LOG_INFO("Ignoring Alert Bell from non-direct message");
            return;
        }
        if (getRtDynanmicEnable()) {
            LOG_INFO("Found Alert Bell, Dynamic Rangetest OFF ");
            setRtDynamicEnable(0);
        } else {
            LOG_INFO("Found Alert Bell, Dynamic Rangetest ON ");
            setRtDynamicEnable(1);
        }
        return;
    }

    if (!((payload[0] == 'A' || payload[0] == 'a') && (payload[1] == 'D' || payload[1] == 'd')))
        return;
    // this is an ADMIN message
    new_size = (size < MAX_ADMIN_MSG) ? size : MAX_ADMIN_MSG;
    strncpy(local_payload, payload, new_size);
    local_payload[new_size] = '\0';

    for (int i = 0; i < new_size; i++) {
        local_payload[i] = tolower(local_payload[i]);
    }

    if (strcmp("adrt on hop", local_payload) == 0 && isToUs) {
        LOG_INFO("Turning Dynamic Rangetest ON with hop");
        setRtDynamicEnable(1);
        setRtHop(1);
    } else if (strcmp("ad help", local_payload) == 0 && isToUs) {
        sendAdminHelp(from);
    } else if (strcmp("adrt on", local_payload) == 0 && isToUs) {
        LOG_INFO("Turning Dynamic Rangetest ON");
        setRtDynamicEnable(1);
        setRtHop(0);
    } else if (strcmp("adrt off", local_payload) == 0 && isToUs) {
        LOG_INFO("Turning Dynamic Rangetest OFF");
        setRtDynamicEnable(0);
    } else if (strncmp("adrt delay", local_payload, 10) == 0 && new_size >= 13 && isToUs) {
        if (strncmp("15", local_payload + 11, 2) == 0) {
            LOG_INFO("Rangetest delay is 15");
            moduleConfig.range_test.sender = 15;
        } else if (strncmp("30", local_payload + 11, 2) == 0) {
            LOG_INFO("Rangetest delay is 30");
            moduleConfig.range_test.sender = 30;
        } else if (strncmp("60", local_payload + 11, 2) == 0) {
            LOG_INFO("Rangetest delay is 60");
            moduleConfig.range_test.sender = 60;
        }
    } else if (strcmp("adar", local_payload) == 0 && isToUs) {
        sendAutoReplyState(from);
    } else if (strcmp("adar on", local_payload) == 0 && isToUs) {
        autoReplyEnabled = true;
        saveAutoReplyState();
        LOG_INFO("Admin auto reply ON (%s)", autoReplyModeName(autoReplyMode));
        sendAutoReplyState(from);
    } else if (strcmp("adar off", local_payload) == 0 && isToUs) {
        autoReplyEnabled = false;
        saveAutoReplyState();
        LOG_INFO("Admin auto reply OFF (%s)", autoReplyModeName(autoReplyMode));
        sendAutoReplyState(from);
    } else if (strcmp("adar all", local_payload) == 0 && isToUs) {
        autoReplyMode = AutoReplyMode::ALL;
        autoReplyEnabled = true;
        saveAutoReplyState();
        LOG_INFO("Admin auto reply ON (ALL)");
        sendAutoReplyState(from);
    } else if (strcmp("adar prim", local_payload) == 0 && isToUs) {
        autoReplyMode = AutoReplyMode::PRIM;
        autoReplyEnabled = true;
        saveAutoReplyState();
        LOG_INFO("Admin auto reply ON (PRIM)");
        sendAutoReplyState(from);
    } else if ((strcmp("adar prim+dm", local_payload) == 0 || strcmp("adar prim dm", local_payload) == 0) && isToUs) {
        autoReplyMode = AutoReplyMode::PRIM_DM;
        autoReplyEnabled = true;
        saveAutoReplyState();
        LOG_INFO("Admin auto reply ON (PRIM+DM)");
        sendAutoReplyState(from);
    } else if (strcmp("adar sec", local_payload) == 0 && isToUs) {
        autoReplyMode = AutoReplyMode::SEC;
        autoReplyEnabled = true;
        saveAutoReplyState();
        LOG_INFO("Admin auto reply ON (SEC)");
        sendAutoReplyState(from);
    } else if ((strcmp("adar sec+dm", local_payload) == 0 || strcmp("adar sec dm", local_payload) == 0) && isToUs) {
        autoReplyMode = AutoReplyMode::SEC_DM;
        autoReplyEnabled = true;
        saveAutoReplyState();
        LOG_INFO("Admin auto reply ON (SEC+DM)");
        sendAutoReplyState(from);
    } else if (strcmp("adar toggle", local_payload) == 0 && isToUs) {
        autoReplyEnabled = !autoReplyEnabled;
        saveAutoReplyState();
        LOG_INFO("Admin auto reply %s (%s)", autoReplyEnabled ? "ON" : "OFF", autoReplyModeName(autoReplyMode));
        sendAutoReplyState(from);
    }
#ifdef FLAMINGO_CONNECTION_LED
    else if (strncmp("adled default ", local_payload, 14) == 0) {
        char color_str[20];
        if (sscanf(local_payload + 14, "%s", color_str) == 1) {
            LEDColor color;
            bool valid = false;
            if (strcmp(color_str, "off") == 0) {
                color = LEDColor::Off;
                valid = true;
            } else if (strcmp(color_str, "red") == 0) {
                color = LEDColor::Red;
                valid = true;
            } else if (strcmp(color_str, "green") == 0) {
                color = LEDColor::Green;
                valid = true;
            } else if (strcmp(color_str, "yellow") == 0 || strcmp(color_str, "amber") == 0) {
                color = LEDColor::Amber;
                valid = true;
            } else if (strcmp(color_str, "blue") == 0) {
                color = LEDColor::Blue;
                valid = true;
            } else if (strcmp(color_str, "purple") == 0) {
                color = LEDColor::Purple;
                valid = true;
            } else if (strcmp(color_str, "teal") == 0) {
                color = LEDColor::Teal;
                valid = true;
            } else if (strcmp(color_str, "white") == 0) {
                color = LEDColor::White;
                valid = true;
            }
            if (valid) {
                LOG_INFO("Setting Connection LED default to %s", color_str);
                if (blinkModule) {
                    blinkModule->setConnectionLEDDefault(color);
                }
            } else {
                LOG_INFO("Invalid color for default: %s", color_str);
            }
        }
    } else if (strncmp("adled ", local_payload, 6) == 0) {
        char color_str[20];
        unsigned long timeout = 0;
        int num = sscanf(local_payload + 6, "%s %lu", color_str, &timeout);
        if (num >= 1) {
            LEDColor color;
            bool valid = false;
            if (strcmp(color_str, "off") == 0) {
                color = LEDColor::Off;
                valid = true;
            } else if (strcmp(color_str, "red") == 0) {
                color = LEDColor::Red;
                valid = true;
            } else if (strcmp(color_str, "green") == 0) {
                color = LEDColor::Green;
                valid = true;
            } else if (strcmp(color_str, "yellow") == 0 || strcmp(color_str, "amber") == 0) {
                color = LEDColor::Amber;
                valid = true;
            } else if (strcmp(color_str, "blue") == 0) {
                color = LEDColor::Blue;
                valid = true;
            } else if (strcmp(color_str, "purple") == 0) {
                color = LEDColor::Purple;
                valid = true;
            } else if (strcmp(color_str, "teal") == 0) {
                color = LEDColor::Teal;
                valid = true;
            } else if (strcmp(color_str, "white") == 0) {
                color = LEDColor::White;
                valid = true;
            }
            if (valid) {
                if (num == 2 && timeout > 0) {
                    timeout *= 1000;
                    LOG_INFO("Setting Connection LED to %s with timeout %lu seconds", color_str, timeout / 1000);
                    if (blinkModule) {
                        blinkModule->setConnectionLED_cooldown(color, timeout);
                    }
                } else if (num == 1) {
                    LOG_INFO("Setting Connection LED to %s", color_str);
                    if (blinkModule) {
                        blinkModule->setConnectionLED(color);
                    }
                }
            } else {
                LOG_INFO("Invalid color: %s", color_str);
            }
        }
    }
#endif
}

#ifdef DEBUG_PORT
char textmsg[201];
#endif

#endif

ProcessMessage TextMessageModule::handleReceived(const meshtastic_MeshPacket &mp)
{
#ifdef FLAMINGO
    /*
     Improve debug messages so that can parsed as part of log at Incident Command
    */
#if defined(DEBUG_PORT) && !defined(DEBUG_MUTE)
    auto &p = mp.decoded;
    meshtastic_NodeInfoLite *n = nodeDB->getMeshNode(getFrom(&mp));

    LOG_INFO("TextModule msg: from=0x%0x, id=0x%x, ln=%s, rssi=%d, rxSNR=%g, hop_limit=%d, hop_start=%d", mp.from, mp.id,
             n->user.long_name, mp.rx_rssi, mp.rx_snr, mp.hop_limit, mp.hop_start);
    uint16_t offset;
    uint16_t bytes_left = p.payload.size;
    bool do_loop = 1;
    offset = 0;
    /* apparently, the maximum size log message is about 150 characters. Deal this with this*/
    while (do_loop) {
        if (bytes_left <= 150) {
            memset(textmsg, 0, sizeof(textmsg));
            strncpy(textmsg, (char *)(p.payload.bytes + offset), bytes_left);
            do_loop = 0;
        } else {
            memset(textmsg, 0, sizeof(textmsg));
            strncpy(textmsg, (char *)(p.payload.bytes + offset), 150);
            offset = offset + 150;
            bytes_left = bytes_left - 150;
        }
        LOG_INFO("z=%s", textmsg);
    }
    // ADRT commands are accepted only on direct messages; ADLED is accepted on direct and channel messages
    parseAdmin(p.payload.size, (char *)p.payload.bytes, isToUs(&mp), mp.from);

#endif
#else
#if defined(DEBUG_PORT) && !defined(DEBUG_MUTE)
    auto &p = mp.decoded;
    LOG_INFO("Received text msg from=0x%0x, id=0x%x, msg=%.*s", mp.from, mp.id, p.payload.size, p.payload.bytes);
#endif
#endif
    // add packet ID to the rolling list of packets
    textPacketList[textPacketListIndex] = mp.id;
    textPacketListIndex = (textPacketListIndex + 1) % TEXT_PACKET_LIST_SIZE;

#ifdef FLAMINGO
    if (mp.which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
        mp.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP && autoReplyEnabled && mp.from != 0 && !isFromUs(&mp) &&
        !mp.decoded.emoji) {
        const bool isDm = !isBroadcast(mp.to);
        const bool isPrimary = (mp.channel == 0);
        bool modeAllows = false;

        switch (autoReplyMode) {
        case AutoReplyMode::ALL:
            modeAllows = true;
            break;
        case AutoReplyMode::PRIM:
            modeAllows = !isDm && isPrimary;
            break;
        case AutoReplyMode::PRIM_DM:
            modeAllows = isDm || isPrimary;
            break;
        case AutoReplyMode::SEC:
            modeAllows = !isDm && !isPrimary;
            break;
        case AutoReplyMode::SEC_DM:
            modeAllows = isDm || !isPrimary;
            break;
        default:
            modeAllows = true;
            break;
        }

        if (modeAllows) {
            sendAutoReply(mp.from, isBroadcast(mp.to), mp.channel, mp.id);
        }
    }
#endif

    // We only store/display messages destined for us.
    devicestate.rx_text_message = mp;
    devicestate.has_rx_text_message = true;
    IF_SCREEN(
        // Guard against running in MeshtasticUI or with no screen
        if (config.display.displaymode != meshtastic_Config_DisplayConfig_DisplayMode_COLOR) {
            // Store in the central message history
            const StoredMessage &sm = messageStore.addFromPacket(mp);

            // Pass message to renderer (banner + thread switching + scroll reset)
            // Use the global Screen singleton to retrieve the current OLED display
            auto *display = screen ? screen->getDisplayDevice() : nullptr;
            graphics::MessageRenderer::handleNewMessage(display, sm, mp);
        })
    // Only trigger screen wake if configuration allows it
    if (shouldWakeOnReceivedMessage()) {
        powerFSM.trigger(EVENT_RECEIVED_MSG);
    }

    // Notify any observers (e.g. external modules that care about packets)
    notifyObservers(&mp);

    return ProcessMessage::CONTINUE; // Let others look at this message also if they want
}

bool TextMessageModule::wantPacket(const meshtastic_MeshPacket *p)
{
    return MeshService::isTextPayload(p);
}

bool TextMessageModule::recentlySeen(uint32_t id)
{
    for (size_t i = 0; i < TEXT_PACKET_LIST_SIZE; i++) {
        if (textPacketList[i] != 0 && textPacketList[i] == id) {
            return true;
        }
    }
    return false;
}