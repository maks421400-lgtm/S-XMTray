#pragma once
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

namespace sony {
using Bytes = std::vector<uint8_t>;
struct Frame { uint8_t type, seq; Bytes payload; };
inline Bytes encode(uint8_t type, uint8_t seq, const Bytes& payload) {
    if (payload.size() > 4096 || seq > 1) throw std::runtime_error("Invalid frame");
    const auto n = static_cast<uint32_t>(payload.size());
    Bytes body{type, seq, uint8_t(n >> 24), uint8_t(n >> 16), uint8_t(n >> 8), uint8_t(n)};
    body.insert(body.end(), payload.begin(), payload.end());
    uint8_t sum = 0;
    for (auto b : body) sum = uint8_t(sum + b);
    body.push_back(sum);
    Bytes wire{0x3e};
    for (auto b : body) {
        if (b >= 0x3c && b <= 0x3e) { wire.push_back(0x3d); wire.push_back(b & 0xef); }
        else wire.push_back(b);
    }
    wire.push_back(0x3c);
    return wire;
}
// Incremental, bounded parser. Invalid or truncated frames never reach the device state.
class Decoder {
    Bytes body;
    bool active = false, escaped = false;
public:
    std::optional<Frame> feed(uint8_t b) {
        if (b == 0x3e) { body.clear(); active = true; escaped = false; return {}; }
        if (!active) return {};
        if (b == 0x3c) {
            active = false;
            if (escaped || body.size() < 7 || body[1] > 1) return {};
            uint32_t n = (uint32_t(body[2]) << 24) | (uint32_t(body[3]) << 16) |
                         (uint32_t(body[4]) << 8) | body[5];
            if (n > 4096 || body.size() != n + 7) return {};
            uint8_t sum = 0;
            for (size_t i = 0; i + 1 < body.size(); ++i) sum = uint8_t(sum + body[i]);
            if (sum != body.back()) return {};
            return Frame{body[0], body[1], Bytes(body.begin() + 6, body.end() - 1)};
        }
        if (escaped) {
            if (b < 0x2c || b > 0x2e) { active = false; return {}; }
            body.push_back(b | 0x10); escaped = false;
        } else if (b == 0x3d) escaped = true;
        else body.push_back(b);
        if (body.size() > 4103) active = false;
        return {};
    }
};
enum class Mode { Unknown = -1, Off = 0, Anc = 1, Ambient = 2 };
struct NoiseState {
    Mode mode = Mode::Unknown;
    uint8_t focus = 0, level = 0;
};
inline std::optional<NoiseState> parseNoise(const Bytes& p) {
    if (p.size() != 7 || (p[0] != 0x67 && p[0] != 0x69) || p[1] != 0x17 ||
        p[2] > 1 || p[3] > 1 || p[4] > 1 || p[5] > 1 || p[6] > 20) return {};
    return NoiseState{p[3] == 0 ? Mode::Off : p[4] == 0 ? Mode::Anc : Mode::Ambient, p[5], p[6]};
}
inline Bytes setNoise(Mode mode, const NoiseState& current) {
    if (current.mode == Mode::Unknown || mode == Mode::Unknown) throw std::runtime_error("Unknown noise mode");
    // Keep the user's ambient level and voice-focus setting unchanged.
    return {0x68, 0x17, 0x01, uint8_t(mode != Mode::Off),
            uint8_t(mode == Mode::Ambient || (mode == Mode::Off && current.mode == Mode::Ambient)),
            current.focus, current.level};
}
inline const char* modeName(Mode m) {
    switch (m) { case Mode::Anc: return "anc"; case Mode::Ambient: return "ambient";
        case Mode::Off: return "off"; default: return "unknown"; }
}
}
