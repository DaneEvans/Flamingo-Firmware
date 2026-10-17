#pragma once

#ifdef FLAMINGO
#include "MeshTypes.h"
#include <cstddef>

namespace flamingo::adar
{
void loadState();
bool handleAdminMessage(const char *payload, size_t size, bool isToUs, NodeNum from, uint8_t channel, uint32_t replyId);
void handleIncomingMessage(const meshtastic_MeshPacket &mp);
} // namespace flamingo::adar

#endif
