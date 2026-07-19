#pragma once
#include <cstdint>

namespace Screenshare {

// Main packet types sent over the wire.
enum PacketType : uint8_t {
    PACKET_TYPE_FRAME = 1,
    PACKET_TYPE_CURSOR = 3,
};

// Frame sub-types, sent within a frame packet.
enum FrameType : uint8_t {
    FRAME_TYPE_DIFF = 1,
    FRAME_TYPE_FULL = 2,
};

} // namespace Screenshare