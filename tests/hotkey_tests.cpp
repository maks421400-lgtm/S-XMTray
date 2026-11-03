#include "../src/hotkeys.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
void check(bool ok, const char* label) { if (!ok) throw std::runtime_error(label); }
struct TestWindow {
    HWND hwnd = CreateWindowExW(0, L"STATIC", L"QuietTray hotkey test", 0, 0,0,0,0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    ~TestWindow() { if (hwnd) DestroyWindow(hwnd); }
};
int main(int argc, char** argv) {
    using namespace quiet;
    try {
        check(bool(validateHotkeys({})), "empty disabled default");
        check(validateHotkeys({true,{}}).error == HotkeyError::Empty, "enabled empty rejected");
        check(validateHotkeys({true,{0x0641,0x0641,0}}).error == HotkeyError::Duplicate, "duplicate chord");
        check(validateHotkeys({true,{0x062e,0x0e2e,0}}).error == HotkeyError::Duplicate, "extended bit is not a distinct global shortcut");
        check(validateHotkeys({true,{0x0041,0,0}}).error == HotkeyError::Invalid, "plain letters cannot be captured globally");
        check(validateHotkeys({true,{0x0141,0,0}}).error == HotkeyError::Invalid, "shift alone rejected");
        check(validateHotkeys({true,{0x067b,0,0}}).error == HotkeyError::Invalid, "F12 reserved");
        check(validateHotkeys({true,{0xffff0641,0,0}}).error == HotkeyError::Invalid, "corrupt persisted value");
        check(hotkeyModifiers(0x0741) == (MOD_CONTROL|MOD_ALT|MOD_SHIFT|MOD_NOREPEAT), "control flags converted, no auto-repeat");
        if (argc == 1) {
            std::cout << "All hotkey validation tests passed. Use --windows for OS registration tests.\n";
            return 0;
        }
        check(argc == 2 && std::string(argv[1]) == "--windows", "Usage: hotkey-tests [--windows]");
        TestWindow owner, competitor;
        check(owner.hwnd && competitor.hwnd, "test windows");
        HotkeyRegistration registration(owner.hwnd);
        std::vector<uint32_t> available;
        // Find unused combinations, never displacing another application's registration.
        for (UINT vk = VK_F13; vk <= VK_F24 && available.size() < 3; ++vk) {
            if (RegisterHotKey(competitor.hwnd, 10, MOD_CONTROL|MOD_ALT|MOD_SHIFT|MOD_NOREPEAT, vk)) {
                available.push_back(0x0700 | vk); UnregisterHotKey(competitor.hwnd, 10);
            }
        }
        check(available.size() == 3, "three free test chords");
        HotkeyConfig cfg{true,{available[0],available[1],available[2]}};
        check(bool(registration.apply(cfg)), "register three hotkeys");
        LPARAM event = MAKELPARAM(MOD_CONTROL|MOD_ALT|MOD_SHIFT, available[0] & 0xff);
        check(registration.accepts(500,event,cfg), "registered event accepted");
        check(!registration.accepts(499,event,cfg), "unrelated id ignored");
        check(!registration.accepts(500,MAKELPARAM(MOD_ALT,'A'),cfg), "stale key event ignored");
        check(!RegisterHotKey(competitor.hwnd, 10, hotkeyModifiers(available[0]), available[0]&0xff), "OS owns registered shortcut");
        auto disabled = cfg; disabled.enabled = false;
        check(bool(registration.apply(disabled)), "disable");
        check(!registration.accepts(500,event,disabled), "disabled event ignored");
        check(RegisterHotKey(competitor.hwnd, 10, hotkeyModifiers(available[1]), available[1]&0xff) != 0, "disabled shortcut released");
        auto failure = registration.apply(cfg);
        check(failure.error == HotkeyError::Unavailable && failure.index == 1, "conflict reported on correct action");
        check(RegisterHotKey(competitor.hwnd, 11, hotkeyModifiers(available[0]), available[0]&0xff) != 0, "partial candidate rolled back");
        UnregisterHotKey(competitor.hwnd, 10); UnregisterHotKey(competitor.hwnd, 11);
        check(bool(registration.apply(cfg)), "registration recovers after conflict");
        registration.clear();
        check(!registration.accepts(500,event,cfg), "editing suspends shortcuts");
        check(bool(registration.apply(cfg)), "cancel restores original shortcuts");
        auto cleared = cfg; cleared.keys[0] = 0;
        check(bool(registration.apply(cleared)), "clear individual action");
        check(!registration.accepts(500,event,cleared), "cleared action ignored");
        check(RegisterHotKey(competitor.hwnd, 10, hotkeyModifiers(available[0]), available[0]&0xff) != 0, "cleared shortcut released");
        UnregisterHotKey(competitor.hwnd, 10);
        registration.clear();
        std::cout << "All hotkey validation and Windows registration tests passed.\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
