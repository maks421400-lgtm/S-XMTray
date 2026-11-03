#include "bluetooth.hpp"
#include <thread>
#include <algorithm>

namespace sony {
using Clock = std::chrono::steady_clock;
static void fail(const char* what, int code) {
    throw std::runtime_error(std::string(what) + " (" + std::to_string(code) + ")");
}
static int waitSocket(SOCKET s, bool write, int ms) {
    fd_set ready, error;
    FD_ZERO(&ready); FD_SET(s, &ready); FD_ZERO(&error); FD_SET(s, &error);
    timeval t{ms / 1000, (ms % 1000) * 1000};
    const int r = select(0, write ? nullptr : &ready, write ? &ready : nullptr, &error, &t);
    if (r == SOCKET_ERROR) fail("Bluetooth select", WSAGetLastError());
    if (FD_ISSET(s, &error)) {
        int code = 0, n = sizeof(code);
        getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&code), &n);
        fail("Bluetooth connection", code);
    }
    return r;
}
void Session::close() {
    if (socket_ != INVALID_SOCKET) { closesocket(socket_); socket_ = INVALID_SOCKET; }
    state = {}; decoder = {}; replies.clear(); lastRx.reset(); seq = 0; awaiting = acknowledged = false;
    features={}; supported.clear(); batteryType.reset(); caseType.reset(); sidetoneType.reset();
}
void Session::connectDevice() {
    close();
    BLUETOOTH_DEVICE_SEARCH_PARAMS search{sizeof(search)};
    search.fReturnAuthenticated = search.fReturnRemembered = search.fReturnConnected = TRUE;
    BLUETOOTH_DEVICE_INFO info{sizeof(info)};
    auto h = BluetoothFindFirstDevice(&search, &info);
    if (!h) fail("Bluetooth enumeration", GetLastError());
    BTH_ADDR address = 0;
    unsigned matches = 0;
    do {
        if (wcscmp(info.szName, L"WF-1000XM5") == 0 && info.fConnected && info.fAuthenticated) {
            address = info.Address.ullLong; ++matches;
        }
    } while (BluetoothFindNextDevice(h, &info));
    BluetoothFindDeviceClose(h);
    if (!address) throw std::runtime_error("WF-1000XM5 is not connected");
    if (matches > 1) throw std::runtime_error("More than one WF-1000XM5 connected");
    socket_ = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
    if (socket_ == INVALID_SOCKET) fail("Bluetooth socket", WSAGetLastError());
    try {
        ULONG enabled = 1;
        if (ioctlsocket(socket_, FIONBIO, &enabled)) fail("Bluetooth nonblocking mode", WSAGetLastError());
        setsockopt(socket_, SOL_RFCOMM, SO_BTH_AUTHENTICATE, reinterpret_cast<char*>(&enabled), sizeof(enabled));
        setsockopt(socket_, SOL_RFCOMM, SO_BTH_ENCRYPT, reinterpret_cast<char*>(&enabled), sizeof(enabled));
        SOCKADDR_BTH target{};
        target.addressFamily = AF_BTH; target.btAddr = address;
        target.serviceClassId = {0x956c7b26, 0xd49a, 0x4ba8, {0xb0,0x3f,0xb1,0x7d,0x39,0x3c,0xb6,0xe2}};
        if (::connect(socket_, reinterpret_cast<sockaddr*>(&target), sizeof(target)) == SOCKET_ERROR) {
            int e = WSAGetLastError();
            if (e != WSAEWOULDBLOCK && e != WSAEINPROGRESS) fail("Bluetooth connect", e);
            const auto end = Clock::now() + std::chrono::seconds(8);
            while (!waitSocket(socket_, true, 100)) {
                if (cancelled && cancelled()) throw std::runtime_error("Cancelled");
                if (Clock::now() >= end) throw std::runtime_error("Bluetooth connect timed out");
            }
        }
        int error = 0, size = sizeof(error);
        if (getsockopt(socket_, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &size)) fail("Bluetooth status", WSAGetLastError());
        if (error) fail("Bluetooth connect", error);
        auto protocol = request({0x00,0x00}, 0x01);
        if (protocol.size() != 8) throw std::runtime_error("Unsupported Sony protocol version");
        refresh();
        discoverFeatures();
    } catch (...) { close(); throw; }
}
void Session::sendFrame(uint8_t type, uint8_t sequence, const Bytes& payload) {
    auto wire = encode(type, sequence, payload);
    size_t sent = 0;
    const auto deadline = Clock::now() + std::chrono::seconds(3);
    while (sent < wire.size()) {
        if (cancelled && cancelled()) throw std::runtime_error("Cancelled");
        if (Clock::now() >= deadline) throw std::runtime_error("Bluetooth send timed out");
        if (!waitSocket(socket_, true, 100)) continue;
        int n = send(socket_, reinterpret_cast<const char*>(wire.data() + sent), static_cast<int>(wire.size() - sent), 0);
        if (n == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) continue;
        if (n <= 0) fail("Bluetooth send", WSAGetLastError());
        sent += n;
    }
}
void Session::poll(int timeoutMs) {
    if (!waitSocket(socket_, false, timeoutMs)) return;
    uint8_t buffer[2048];
    int n = recv(socket_, reinterpret_cast<char*>(buffer), sizeof(buffer), 0);
    if (n == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) return;
    if (n <= 0) fail("Bluetooth disconnected", n == 0 ? 0 : WSAGetLastError());
    for (int i = 0; i < n; ++i) {
        auto f = decoder.feed(buffer[i]);
        if (!f) continue;
        if (f->type == 0x01) {
            if (awaiting && f->payload.empty() && f->seq == 1 - seq) {
                seq = f->seq; acknowledged = true; awaiting = false;
            }
        } else if (f->type == 0x0c || f->type == 0x0e) {
            sendFrame(0x01, uint8_t(1 - f->seq), {});
            if (lastRx && *lastRx == f->seq) continue;
            lastRx = f->seq;
            if (f->type == 0x0c) {
                handleFeatures(f->payload);
                if (auto ns = parseNoise(f->payload)) {
                    state = *ns;
                    if (changed) changed(state);
                }
                if (replies.size() >= 64) replies.erase(replies.begin());
                replies.push_back(std::move(*f));
            }
        }
    }
}
Bytes Session::request(const Bytes& payload, uint8_t response) {
    replies.clear(); acknowledged = false; awaiting = true;
    sendFrame(0x0c, seq, payload);
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (Clock::now() < deadline) {
        if (cancelled && cancelled()) throw std::runtime_error("Cancelled");
        poll(100);
        if (!acknowledged) continue;
        if (response == 0) return {};
        for (const auto& f : replies) {
            if (f.payload.size() >= 2 && f.payload[0] == response && f.payload[1] == payload[1]) return f.payload;
        }
    }
    if(acknowledged) throw FeatureError("Headphones did not return the requested setting");
    throw std::runtime_error("Sony command timed out; close Sound Connect on your phone and retry");
}
NoiseState Session::refresh() {
    auto p = request({0x66,0x17}, 0x67);
    auto parsed = parseNoise(p);
    if (!parsed) throw std::runtime_error("Unsupported noise-control reply");
    return state = *parsed;
}
void Session::setMode(Mode mode) {
    refresh();
    request(setNoise(mode, state), 0);
    // Readback, not an ACK, determines whether the mode actually changed.
    for (int i = 0; i < 4; ++i) {
        if (refresh().mode == mode) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
    throw FeatureError("Headphones did not confirm the requested mode");
}
void Session::handleFeatures(const Bytes& p) {
    bool updated=false;
    try {
        updated=parseBattery(p,features) || parseConnections(p,features);
        if(supported.count(0xfc)) if(auto value=parseSpeak(p)) { features.speak=*value; updated=true; }
        if(sidetoneType) if(auto value=parseSidetone(p,*sidetoneType)) { features.sidetone=*value; updated=true; }
        if(supported.count(0x50)) {
            if(auto eq=parseEq(p)) { features.equalizer=*eq; updated=true; }
            if(p.size()==3 && (p[0]==0x53 || p[0]==0x55) && p[1]==0 && p[2]<=1) {
                features.eqAvailable=p[2]==0; updated=true;
            }
        }
    } catch(const FeatureError& e) { features.warning=e.what(); updated=true; }
    if(updated && featuresChanged) featuresChanged(features);
}
void Session::discoverFeatures() {
    try { supported=parseSupport(request({0x06,0},0x07)); }
    catch(const FeatureError& e) { features.warning=e.what(); if(featuresChanged) featuresChanged(features); return; }
    if(supported.count(0x29)) batteryType=uint8_t(9); else if(supported.count(0x21)) batteryType=uint8_t(1);
    if(supported.count(0x2a)) caseType=uint8_t(10); else if(supported.count(0x22)) caseType=uint8_t(2);
    try { features.firmware=parseFirmware(request({4,2},5)); }
    catch(const FeatureError& e) { features.warning=e.what(); }
    for(unsigned id=0xd1;id<=0xd4;++id) if(supported.count(uint8_t(id))) {
        try { if(isSidetoneCapability(request({0xd0,uint8_t(id),1},0xd1),uint8_t(id))) sidetoneType=uint8_t(id); }
        catch(const FeatureError& e) { features.warning=e.what(); }
    }
    if(supported.count(0x50)) {
        try { features.eqCapabilities=parseEqCapabilities(request({0x50,0,1},0x51)); }
        catch(const FeatureError& e) { features.warning=e.what(); }
    }
    refreshFeatures();
}
void Session::refreshFeatures() {
    auto optionalRead=[&](auto read,auto invalidate) {
        try { read(); } catch(const FeatureError& e) { invalidate(); features.warning=e.what(); }
    };
    if(supported.count(0x11)) optionalRead([&]{
        if(!parseConnections(request({0x12,1},0x13),features)) throw FeatureError("Invalid earbud connection state");
    },[&]{features.leftConnected.reset();features.rightConnected.reset();});
    if(batteryType) optionalRead([&]{ if(!parseBattery(request({0x22,*batteryType},0x23),features)) throw FeatureError("Missing L/R battery"); },[&]{features.left={};features.right={};});
    if(caseType) optionalRead([&]{ if(!parseBattery(request({0x22,*caseType},0x23),features)) throw FeatureError("Missing case battery"); },[&]{features.cradle={};});
    if(supported.count(0xfc)) optionalRead([&]{
        features.speak=parseSpeak(request({0xf6,0x0c},0xf7));
        if(!features.speak) throw FeatureError("Invalid Speak-to-Chat reply");
    },[&]{features.speak.reset();});
    if(sidetoneType) optionalRead([&]{
        features.sidetone=parseSidetone(request({0xd6,*sidetoneType},0xd7),*sidetoneType);
        if(!features.sidetone) throw FeatureError("Invalid sidetone reply");
    },[&]{features.sidetone.reset();});
    if(!features.eqCapabilities.presets.empty()) optionalRead([&]{
        auto p=request({0x52,0},0x53);
        if(p.size()!=3 || p[2]>1) throw FeatureError("Invalid EQ availability reply");
        features.eqAvailable=p[2]==0;
        readEq();
    },[&]{features.equalizer.reset();features.eqAvailable.reset();});
    if(featuresChanged) featuresChanged(features);
}
void Session::setSpeak(bool enabled) {
    if(!supported.count(0xfc)) throw FeatureError("Speak-to-Chat is not supported");
    auto p=request({0xf6,0x0c},0xf7);
    if(!parseSpeak(p)) throw FeatureError("Unknown Speak-to-Chat state");
    p[0]=0xf8; p[2]=enabled?0:1; // Preserve preview and all other settings.
    request(p,0);
    for(int i=0;i<4;++i) {
        auto value=parseSpeak(request({0xf6,0x0c},0xf7));
        if(value && *value==enabled) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
    throw FeatureError("Speak-to-Chat change was not confirmed");
}
void Session::setSidetone(bool enabled) {
    if(!sidetoneType) throw FeatureError("Sidetone was not identified on these headphones");
    auto p=request({0xd6,*sidetoneType},0xd7);
    if(!parseSidetone(p,*sidetoneType)) throw FeatureError("Unknown sidetone state");
    p[0]=0xd8; p[3]=enabled?0:1; request(p,0);
    for(int i=0;i<4;++i) {
        auto value=parseSidetone(request({0xd6,*sidetoneType},0xd7),*sidetoneType);
        if(value && *value==enabled) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
    throw FeatureError("Sidetone change was not confirmed");
}
EqState Session::readEq() {
    auto eq=parseEq(request({0x56,0},0x57));
    if(!eq) throw FeatureError("Unsupported equalizer state");
    features.equalizer=*eq; return *eq;
}
void Session::setEqPreset(uint8_t preset) {
    if(std::find(features.eqCapabilities.presets.begin(),features.eqCapabilities.presets.end(),preset)==features.eqCapabilities.presets.end())
        throw FeatureError("Preset was not advertised by these headphones");
    auto available=request({0x52,0},0x53);
    if(available.size()!=3 || available[2]!=0) throw FeatureError("Equalizer is unavailable right now");
    request({0x58,0,preset,0},0);
    for(int i=0;i<4;++i) {
        if(readEq().preset==preset) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
    throw FeatureError("Equalizer preset change was not confirmed");
}
void Session::setEqBands(const EqState& target) {
    if(target.preset!=0xa0 && target.preset!=0xa1 && target.preset!=0xa2) throw FeatureError("Manual EQ requires a custom preset");
    if(std::find(features.eqCapabilities.presets.begin(),features.eqCapabilities.presets.end(),target.preset)==features.eqCapabilities.presets.end())
        throw FeatureError("Custom preset is not supported");
    auto available=request({0x52,0},0x53);
    if(available.size()!=3 || available[2]!=0) throw FeatureError("Equalizer is unavailable right now");
    request(eqBandsCommand(target),0);
    for(int i=0;i<4;++i) {
        if(readEq()==target) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
    throw FeatureError("Equalizer band change was not confirmed");
}
std::vector<std::pair<std::string, Bytes>> Session::inspectFeatures() {
    std::vector<std::pair<std::string, Bytes>> result;
    auto support = request({0x06,0x00},0x07);
    result.push_back({"Supported functions",support});
    if (support.size() < 3 || support.size() != 3 + size_t(support[2])*2)
        throw std::runtime_error("Malformed support list");
    auto has = [&](uint8_t id) { for(size_t i=3;i<support.size();i+=2) if(support[i]==id) return true; return false; };
    result.push_back({"Firmware",request({0x04,0x02},0x05)});
    if (has(0x21)) result.push_back({"L/R battery",request({0x22,0x01},0x23)});
    if (has(0x22)) result.push_back({"Case battery",request({0x22,0x02},0x23)});
    if (has(0x29)) result.push_back({"L/R battery thresholds",request({0x22,0x09},0x23)});
    if (has(0x2a)) result.push_back({"Case battery threshold",request({0x22,0x0a},0x23)});
    if (has(0xfc)) result.push_back({"Speak-to-Chat",request({0xf6,0x0c},0xf7)});
    for (uint8_t id : std::array<uint8_t,4>{0xd1,0xd2,0xd3,0xd4}) if (has(id)) {
        result.push_back({"General capability",request({0xd0,id,0x01},0xd1)});
        result.push_back({"General value",request({0xd6,id},0xd7)});
    }
    if (has(0xf8)) result.push_back({"Call settings",request({0xf6,0x08},0xf7)});
    if (has(0x50)) {
        result.push_back({"EQ capabilities",request({0x50,0x00,0x01},0x51)});
        result.push_back({"EQ state",request({0x56,0x00},0x57)});
    }
    return result;
}
}
