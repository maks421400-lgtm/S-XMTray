#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <array>
#include <cstdint>

namespace quiet {
// Same representation as the Windows hot-key input: VK in low byte,
// HOTKEYF_SHIFT/CONTROL/ALT/EXT in high byte. EXT is display-only.
struct HotkeyConfig {
    bool enabled = false;
    std::array<uint32_t, 3> keys{};
};
enum class HotkeyError { None, Invalid, Duplicate, Empty, Unavailable };
struct HotkeyResult {
    HotkeyError error = HotkeyError::None;
    int index = -1;
    DWORD systemError = 0;
    explicit operator bool() const { return error == HotkeyError::None; }
};
inline HotkeyResult validateHotkeys(const HotkeyConfig& config) {
    bool any = false;
    for (int i = 0; i < 3; ++i) {
        auto packed = config.keys[i];
        if (!packed) continue;
        any = true;
        const auto vk = packed & 0xff, modifiers = packed >> 8;
        if (packed > 0x0fff || vk < VK_BACK || vk == VK_SHIFT || vk == VK_CONTROL ||
            vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN || vk == VK_F12 ||
            (vk >= VK_LSHIFT && vk <= VK_RMENU) || vk == 0xff || !(modifiers & 6))
            return {HotkeyError::Invalid, i};
        for (int j = 0; j < i; ++j)
            if ((packed & 0x07ff) == (config.keys[j] & 0x07ff))
                return {HotkeyError::Duplicate, i};
    }
    if (config.enabled && !any) return {HotkeyError::Empty};
    return {};
}
inline UINT hotkeyModifiers(uint32_t packed) {
    const UINT flags = packed >> 8;
    return ((flags & 1) ? MOD_SHIFT : 0) | ((flags & 2) ? MOD_CONTROL : 0) |
           ((flags & 4) ? MOD_ALT : 0) | MOD_NOREPEAT;
}
// Called only on the UI thread. Failed registration always releases the entire
// candidate set, so the UI never reports a partially enabled configuration.
class HotkeyRegistration {
    HWND owner;
    std::array<bool, 3> active{};
public:
    static constexpr int FirstId = 500;
    explicit HotkeyRegistration(HWND hwnd) : owner(hwnd) {}
    HotkeyRegistration(const HotkeyRegistration&) = delete;
    HotkeyRegistration& operator=(const HotkeyRegistration&) = delete;
    ~HotkeyRegistration() { clear(); }
    void clear() {
        for (int i = 0; i < 3; ++i) {
            if (active[i]) UnregisterHotKey(owner, FirstId + i);
            active[i] = false;
        }
    }
    HotkeyResult apply(const HotkeyConfig& config) {
        clear();
        auto valid = validateHotkeys(config);
        if (!valid) return valid;
        if (!config.enabled) return {};
        for (int i = 0; i < 3; ++i) {
            if (!config.keys[i]) continue;
            if (!RegisterHotKey(owner, FirstId + i, hotkeyModifiers(config.keys[i]), config.keys[i] & 0xff)) {
                DWORD error = GetLastError(); clear();
                return {HotkeyError::Unavailable, i, error};
            }
            active[i] = true;
        }
        return {};
    }
    bool accepts(WPARAM id, LPARAM data, const HotkeyConfig& config) const {
        if (id < FirstId || id >= FirstId + 3 || !config.enabled) return false;
        size_t i = id - FirstId;
        return active[i] && HIWORD(data) == (config.keys[i] & 0xff) &&
            LOWORD(data) == (hotkeyModifiers(config.keys[i]) & ~MOD_NOREPEAT);
    }
};
}
