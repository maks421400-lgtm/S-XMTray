#pragma once
#include "protocol.hpp"
#include <array>
#include <chrono>
#include <set>
#include <string>

namespace sony {
struct FeatureError : std::runtime_error { using std::runtime_error::runtime_error; };
struct Battery {
    std::optional<int> percent;
    uint8_t charging = 2;
    std::chrono::steady_clock::time_point received{};
};
struct EqState { uint8_t preset = 0; std::array<uint8_t,6> bands{}; };
inline bool operator==(const EqState& a,const EqState& b) { return a.preset==b.preset && a.bands==b.bands; }
struct EqCapabilities { std::vector<uint8_t> presets; };
struct FeatureState {
    std::string firmware;
    std::optional<bool> leftConnected, rightConnected;
    Battery left, right, cradle;
    std::optional<bool> speak, sidetone, eqAvailable;
    std::optional<EqState> equalizer;
    EqCapabilities eqCapabilities;
    std::string warning;
};
inline bool parseConnections(const Bytes& p,FeatureState& state) {
    if(p.size()!=4 || (p[0]!=0x13 && p[0]!=0x15) || p[1]!=1 || p[2]>1 || p[3]>1) return false;
    state.leftConnected=p[2]==1; state.rightConnected=p[3]==1; return true;
}
inline std::set<uint8_t> parseSupport(const Bytes& p) {
    if(p.size()<3 || p[0]!=7 || p[1]!=0 || p.size()!=3+size_t(p[2])*2) throw FeatureError("Invalid supported-function list");
    std::set<uint8_t> result;
    for(size_t i=3;i<p.size();i+=2) result.insert(p[i]);
    return result;
}
inline std::string readString(const Bytes& p,size_t& offset) {
    if(offset>=p.size()) throw FeatureError("Missing string length");
    size_t n=p[offset++];
    if(n>p.size()-offset) throw FeatureError("Truncated string");
    std::string s(p.begin()+offset,p.begin()+offset+n); offset+=n; return s;
}
inline std::string parseFirmware(const Bytes& p) {
    if(p.size()<3 || p[0]!=5 || p[1]!=2) throw FeatureError("Invalid firmware reply");
    size_t at=2; auto s=readString(p,at);
    if(at!=p.size() || s.empty() || s.size()>40 || s.find_first_not_of("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-_")!=std::string::npos)
        throw FeatureError("Invalid firmware version");
    return s;
}
inline Battery batteryValue(uint8_t level,uint8_t charging) {
    if((level>100 && level!=255) || (charging>3 && charging!=255)) throw FeatureError("Invalid battery reading");
    return {level<=100 ? std::optional<int>(level) : std::nullopt,charging,std::chrono::steady_clock::now()};
}
inline bool parseBattery(const Bytes& p,FeatureState& state) {
    if(p.size()<2 || (p[0]!=0x23 && p[0]!=0x25)) return false;
    if(p[1]==1 || p[1]==9) {
        if(p.size()!=(p[1]==9 ? 8u:6u)) throw FeatureError("Invalid L/R battery reply");
        auto left=batteryValue(p[2],p[3]),right=batteryValue(p[4],p[5]);
        state.left=left; state.right=right; return true;
    }
    if(p[1]==2 || p[1]==10) {
        if(p.size()!=(p[1]==10 ? 5u:4u)) throw FeatureError("Invalid case battery reply");
        state.cradle=batteryValue(p[2],p[3]); return true;
    }
    return false;
}
inline std::optional<bool> parseSpeak(const Bytes& p) {
    if(p.size()!=4 || (p[0]!=0xf7 && p[0]!=0xf9) || p[1]!=0x0c || p[2]>1 || p[3]>1) return {};
    return p[2]==0; // Sony's setting enums use 0=on, unlike NC/ASM.
}
inline bool isSidetoneCapability(const Bytes& p,uint8_t type) {
    if(p.size()<6 || p[0]!=0xd1 || p[1]!=type || p[2]!=0 || p[3]!=1) return false;
    size_t at=4; auto name=readString(p,at); readString(p,at);
    return at==p.size() && name=="SIDETONE_SETTING";
}
inline std::optional<bool> parseSidetone(const Bytes& p,uint8_t type) {
    if(p.size()!=4 || (p[0]!=0xd7 && p[0]!=0xd9) || p[1]!=type || p[2]!=0 || p[3]>1) return {};
    return p[3]==0;
}
inline EqCapabilities parseEqCapabilities(const Bytes& p) {
    if(p.size()<5 || p[0]!=0x51 || p[1]!=0 || p[2]!=6 || p[3]!=21 || p[4]==0 || p[4]>32)
        throw FeatureError("Unsupported equalizer layout (expected five bands plus Clear Bass)");
    EqCapabilities result; size_t at=5;
    for(size_t i=0;i<p[4];++i) {
        if(at>=p.size()) throw FeatureError("Truncated equalizer presets");
        auto id=p[at++]; readString(p,at);
        for(auto existing:result.presets) if(existing==id) throw FeatureError("Duplicate equalizer preset");
        result.presets.push_back(id);
    }
    if(at!=p.size()) throw FeatureError("Invalid equalizer capabilities");
    return result;
}
inline std::optional<EqState> parseEq(const Bytes& p) {
    if(p.size()!=10 || (p[0]!=0x57 && p[0]!=0x59) || p[1]!=0 || p[3]!=6) return {};
    EqState result; result.preset=p[2];
    for(size_t i=0;i<6;++i) { if(p[4+i]>20) return {}; result.bands[i]=p[4+i]; }
    return result;
}
inline Bytes eqBandsCommand(const EqState& state) {
    Bytes p{0x58,0,state.preset,6};
    for(auto value:state.bands) { if(value>20) throw FeatureError("Equalizer value out of range"); p.push_back(value); }
    return p;
}
}
