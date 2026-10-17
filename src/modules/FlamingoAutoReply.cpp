#ifdef FLAMINGO

#include "FlamingoAutoReply.h"

#include "FSCommon.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "SPILock.h"
#include "configuration.h"
#include <cstring>

#define AUTO_REPLY_PERSIST_PATH "/prefs/adar.proto"

namespace flamingo::adar
{
namespace
{
enum class AutoReplyMode : uint8_t {
    ALL = 0,
    PRIM = 1,
    PRIM_DM = 2,
    SEC = 3,
    SEC_DM = 4,
};

static bool autoReplyEnabled = false;
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

static void sendText(NodeNum to, uint8_t channel, uint32_t replyId, const char *text, bool isEmoji = false)
{
    if (to == 0)
        return;

    meshtastic_MeshPacket *p = router->allocForSending();
    p->to = to;
    p->channel = channel;
    p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    p->want_ack = false;
    p->decoded.want_response = false;
    p->decoded.reply_id = replyId;
    p->decoded.payload.size = strlen(text);
    memcpy(p->decoded.payload.bytes, text, p->decoded.payload.size);
    p->decoded.emoji = isEmoji ? 1 : 0;
    service->sendToMesh(p);
}

static void sendState(NodeNum to, uint8_t channel, uint32_t replyId)
{
    char msg[31];
    snprintf(msg, sizeof(msg), "ADAR %s %s", autoReplyEnabled ? "ON" : "OFF", autoReplyModeName(autoReplyMode));
    sendText(to, channel, replyId, msg);
}

static void sendHelp(NodeNum to, uint8_t channel, uint32_t replyId)
{
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
        sendText(to, channel, replyId, line);
    }
}

static bool modeAllowsPacket(const meshtastic_MeshPacket &mp)
{
    const bool isDm = !isBroadcast(mp.to);
    const bool isPrimary = mp.channel == 0;

    switch (autoReplyMode) {
    case AutoReplyMode::ALL:
        return true;
    case AutoReplyMode::PRIM:
        return !isDm && isPrimary;
    case AutoReplyMode::PRIM_DM:
        return isDm || isPrimary;
    case AutoReplyMode::SEC:
        return !isDm && !isPrimary;
    case AutoReplyMode::SEC_DM:
        return isDm || !isPrimary;
    default:
        return true;
    }
}

static void sendAutoReply(const meshtastic_MeshPacket &mp)
{
    if (!autoReplyEnabled || mp.from == 0 || mp.from == nodeDB->getNodeNum()) {
        return;
    }
    if (!modeAllowsPacket(mp) || mp.decoded.emoji) {
        return;
    }

    const NodeNum to = isBroadcast(mp.to) ? NODENUM_BROADCAST : mp.from;
    static constexpr char robotReply[] = "🤖";
    sendText(to, mp.channel, mp.id, robotReply, true);
}

} // namespace

void loadState()
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

static void saveState()
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

bool handleAdminMessage(const char *payload, size_t size, bool isToUs, NodeNum from, uint8_t channel, uint32_t replyId)
{
    if (!isToUs || !payload || size == 0) {
        return false;
    }

    if (strcmp(payload, "ad help") == 0) {
        sendHelp(from, channel, replyId);
        return true;
    }

    if (strcmp(payload, "adar") == 0) {
        sendState(from, channel, replyId);
        return true;
    }

    if (strcmp(payload, "adar on") == 0) {
        autoReplyEnabled = true;
        saveState();
        sendState(from, channel, replyId);
        return true;
    }
    if (strcmp(payload, "adar off") == 0) {
        autoReplyEnabled = false;
        saveState();
        sendState(from, channel, replyId);
        return true;
    }
    if (strcmp(payload, "adar toggle") == 0) {
        autoReplyEnabled = !autoReplyEnabled;
        saveState();
        sendState(from, channel, replyId);
        return true;
    }

    if (strcmp(payload, "adar all") == 0) {
        autoReplyMode = AutoReplyMode::ALL;
        autoReplyEnabled = true;
        saveState();
        sendState(from, channel, replyId);
        return true;
    }
    if (strcmp(payload, "adar prim") == 0) {
        autoReplyMode = AutoReplyMode::PRIM;
        autoReplyEnabled = true;
        saveState();
        sendState(from, channel, replyId);
        return true;
    }
    if (strcmp(payload, "adar prim+dm") == 0 || strcmp(payload, "adar prim dm") == 0) {
        autoReplyMode = AutoReplyMode::PRIM_DM;
        autoReplyEnabled = true;
        saveState();
        sendState(from, channel, replyId);
        return true;
    }
    if (strcmp(payload, "adar sec") == 0) {
        autoReplyMode = AutoReplyMode::SEC;
        autoReplyEnabled = true;
        saveState();
        sendState(from, channel, replyId);
        return true;
    }
    if (strcmp(payload, "adar sec+dm") == 0 || strcmp(payload, "adar sec dm") == 0) {
        autoReplyMode = AutoReplyMode::SEC_DM;
        autoReplyEnabled = true;
        saveState();
        sendState(from, channel, replyId);
        return true;
    }

    return false;
}

void handleIncomingMessage(const meshtastic_MeshPacket &mp)
{
    sendAutoReply(mp);
}

} // namespace flamingo::adar

#endif
