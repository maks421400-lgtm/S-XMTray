#include "../src/protocol.hpp"
#include <iostream>
#include <stdexcept>
using namespace sony;
void check(bool ok, const char* label) { if (!ok) throw std::runtime_error(label); }
std::vector<Frame> decode(const Bytes& wire) {
    Decoder d; std::vector<Frame> frames;
    for (auto b : wire) if (auto f = d.feed(b)) frames.push_back(*f);
    return frames;
}
int main() {
    try {
        // Independent known wire fixtures, including the ACK expected by the headset.
        check(encode(0x0c,0,{0,0}) == Bytes({0x3e,0x0c,0,0,0,0,2,0,0,0x0e,0x3c}), "protocol handshake fixture");
        check(encode(1,1,{}) == Bytes({0x3e,1,1,0,0,0,0,2,0x3c}), "ACK fixture");
        auto frames = decode({0x99,0x3e,0x0c,1,0,0,0,7,0x67,0x17,1,1,0,0,20,0xa8,0x3c});
        check(frames.size() == 1 && parseNoise(frames[0].payload)->mode == Mode::Anc, "ANC response fixture");
        Bytes all; for (int i = 0; i < 256; ++i) all.push_back(uint8_t(i));
        auto escaped = decode(encode(0x0c,1,all));
        check(escaped.size() == 1 && escaped[0].payload == all, "escape all byte values");
        Decoder fragmented; auto wire = encode(0x0c,0,{0x67,0x17,1,1,1,0,20});
        for (size_t i = 0; i + 1 < wire.size(); ++i) check(!fragmented.feed(wire[i]), "no partial delivery");
        check(fragmented.feed(wire.back()).has_value(), "fragment completion");
        auto broken = wire; broken[7] ^= 1;
        check(decode(broken).empty(), "corrupt checksum rejected");
        broken = wire; broken[5] = 1;
        check(decode(broken).empty(), "wrong length rejected");
        check(decode({0x3e,0x3d,0x55,0x3c}).empty(), "invalid escape rejected");
        Bytes recovery{0x3e}; recovery.insert(recovery.end(), 5000, 0);
        recovery.insert(recovery.end(), wire.begin(), wire.end());
        check(decode(recovery).size() == 1, "oversized frame recovery");
        auto two = wire; two.insert(two.end(), wire.begin(), wire.end());
        check(decode(two).size() == 2, "coalesced frames");
        auto ambient = parseNoise({0x69,0x17,1,1,1,1,12});
        check(ambient && ambient->mode == Mode::Ambient, "ambient notification");
        check(setNoise(Mode::Anc,*ambient) == Bytes({0x68,0x17,1,1,0,1,12}), "preserve ambient preferences");
        check(setNoise(Mode::Off,*ambient) == Bytes({0x68,0x17,1,0,1,1,12}), "disable without losing preferences");
        check(parseNoise({0x67,0x17,1,0,1,0,20})->mode == Mode::Off, "off reply");
        check(!parseNoise({0x67,0x19,1,1,0,0,20}), "unsupported variant");
        check(!parseNoise({0x67,0x17,1,1,2,0,20}), "invalid enum");
        check(!parseNoise({0x67,0x17,1,1,0,0,21}), "invalid level");
        check(!parseNoise({0x67,0x17}), "truncated noise state");
        bool threw = false; try { setNoise(Mode::Anc,{}); } catch (...) { threw = true; }
        check(threw, "cannot write without known state");
        std::cout << "All protocol tests passed.\n"; return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
