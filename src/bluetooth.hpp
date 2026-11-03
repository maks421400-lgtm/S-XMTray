#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2bth.h>
#include <windows.h>
#include <bluetoothapis.h>
#include <chrono>
#include <functional>
#include <string>
#include "protocol.hpp"
#include "features.hpp"

namespace sony {
class Session {
    SOCKET socket_ = INVALID_SOCKET;
    Decoder decoder;
    uint8_t seq = 0;
    bool awaiting = false, acknowledged = false;
    std::optional<uint8_t> lastRx;
    std::vector<Frame> replies;
    void sendFrame(uint8_t type, uint8_t sequence, const Bytes& payload);
    Bytes request(const Bytes& payload, uint8_t response);
    std::set<uint8_t> supported;
    std::optional<uint8_t> batteryType, caseType, sidetoneType;
    void handleFeatures(const Bytes& payload);
    void discoverFeatures();
public:
    NoiseState state;
    FeatureState features;
    std::function<void(const NoiseState&)> changed;
    std::function<void(const FeatureState&)> featuresChanged;
    std::function<bool()> cancelled;
    Session() = default;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    ~Session() { close(); }
    void close();
    void connectDevice();
    void poll(int timeoutMs);
    NoiseState refresh();
    void setMode(Mode mode);
    std::vector<std::pair<std::string, Bytes>> inspectFeatures();
    void refreshFeatures();
    void setSpeak(bool enabled);
    void setSidetone(bool enabled);
    EqState readEq();
    void setEqPreset(uint8_t preset);
    void setEqBands(const EqState& target);
};
struct Winsock {
    Winsock() { WSADATA d{}; if (WSAStartup(MAKEWORD(2,2), &d)) throw std::runtime_error("WSAStartup failed"); }
    ~Winsock() { WSACleanup(); }
};
}
