#include "bluetooth.hpp"
#include <shellapi.h>
#include <shlobj.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <commctrl.h>
#include <memory>
#include "hotkeys.hpp"
#include "resource.h"
#include "theme.hpp"

namespace {
constexpr UINT TrayMessage = WM_APP + 1, StateMessage = WM_APP + 2,
    OpenMenuMessage = WM_APP + 3, OpenAboutMessage = WM_APP + 4, OpenHotkeysMessage = WM_APP + 5;
constexpr UINT Anc = 101, Ambient = 102, Off = 103, Reconnect = 104,
    Exit = 105, About = 106, LangAuto = 110, LangRu = 111, LangEn = 112, Startup = 120, Hotkeys = 121,
    Speak = 130, Sidetone = 131, Equalizer = 132, EqPresetAction = 133, EqBandsAction = 134,
    ThemeAuto = 140, ThemeLight = 141, ThemeDark = 142;
constexpr UINT EqStateMessage = WM_APP + 6, OpenEqualizerMessage = WM_APP + 7;
constexpr UINT OpenPanelMessage = WM_APP + 8;
constexpr UINT OpenSettingsMessage = WM_APP + 9;
HWND window = nullptr;
HWND panel = nullptr;
void showPanel();
void updatePanel();
void showSettings();
void updateSettings();
HWND settingsDialog = nullptr;
NOTIFYICONDATAW tray{};
UINT taskbarCreated = 0;
std::wstring settingsFile;
int language = 0;
quiet::HotkeyConfig hotkeyConfig;
quiet::HotkeyResult hotkeyResult;
std::unique_ptr<quiet::HotkeyRegistration> hotkeyRegistration;
HWND hotkeyDialog = nullptr;
HWND eqDialog = nullptr;
void applyTheme() {
    quiet::ui::refresh();
    for(HWND dialog : {panel, hotkeyDialog, eqDialog, settingsDialog}) if(dialog) quiet::ui::apply(dialog);
}
bool eqDirty = false, eqApplying = false, eqDraftSelection = false;
std::atomic<bool> stopping{false};
std::mutex mutex;
std::condition_variable wake;
std::thread worker;
int pending = 0;
int pendingValue = 0;
sony::EqState pendingBands;
struct View {
    bool connected = false, busy = false;
    sony::Mode mode = sony::Mode::Unknown;
    std::wstring error;
    sony::FeatureState features;
} view;

bool russian() { return language == 1 || (language == 0 && PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN); }
const wchar_t* tr(const wchar_t* ru, const wchar_t* en) { return russian() ? ru : en; }
const wchar_t* modeText(sony::Mode mode) {
    switch (mode) {
        case sony::Mode::Anc: return tr(L"Шумоподавление", L"Noise cancelling");
        case sony::Mode::Ambient: return tr(L"Окружающий звук", L"Ambient sound");
        case sony::Mode::Off: return tr(L"Выключено", L"Off");
        default: return tr(L"Режим неизвестен", L"Unknown mode");
    }
}
const sony::Mode hotkeyModes[] = {sony::Mode::Anc, sony::Mode::Ambient, sony::Mode::Off};
const int hotkeyActions[] = {Anc, Ambient, Off};
const wchar_t* hotkeyNames[] = {L"Anc", L"Ambient", L"Off"};
std::wstring describeHotkeyError(quiet::HotkeyResult result) {
    std::wstring prefix = result.index >= 0 ? std::wstring(modeText(hotkeyModes[result.index])) + L": " : L"";
    switch (result.error) {
        case quiet::HotkeyError::Invalid: return prefix + tr(L"нужны Ctrl и/или Alt и клавиша (кроме F12).", L"use Ctrl and/or Alt plus a key (except F12).");
        case quiet::HotkeyError::Duplicate: return prefix + tr(L"комбинация уже назначена другому режиму.", L"this shortcut is assigned to another mode.");
        case quiet::HotkeyError::Empty: return tr(L"Сначала назначьте хотя бы одну комбинацию.", L"Assign at least one shortcut first.");
        case quiet::HotkeyError::Unavailable: return prefix + tr(L"комбинация занята или недоступна в Windows.", L"shortcut is already in use or unavailable in Windows.");
        default: return L"";
    }
}
std::wstring hotkeyText(uint32_t packed) {
    if (!packed) return L"";
    std::wstring text;
    if (packed & 0x0200) text += L"Ctrl+";
    if (packed & 0x0400) text += L"Alt+";
    if (packed & 0x0100) text += L"Shift+";
    UINT scan = MapVirtualKeyW(packed & 0xff, MAPVK_VK_TO_VSC);
    LONG bits = static_cast<LONG>(scan << 16);
    if (packed & 0x0800) bits |= 1 << 24;
    wchar_t name[64]{};
    if (GetKeyNameTextW(bits, name, 64)) text += name;
    else text += L"VK " + std::to_wstring(packed & 0xff);
    return text;
}
bool saveHotkeys(const quiet::HotkeyConfig& config) {
    std::wstring section = config.enabled ? L"Enabled=1" : L"Enabled=0";
    section.push_back(L'\0');
    for (int i = 0; i < 3; ++i) {
        section += std::wstring(hotkeyNames[i]) + L"=" + std::to_wstring(config.keys[i]);
        section.push_back(L'\0');
    }
    section.push_back(L'\0');
    return WritePrivateProfileSectionW(L"Hotkeys", section.data(), settingsFile.c_str()) != 0;
}
void notifyHotkeyFailure() {
    if (hotkeyResult) return;
    NOTIFYICONDATAW notice = tray;
    notice.uFlags = NIF_INFO; notice.dwInfoFlags = NIIF_WARNING;
    wcsncpy_s(notice.szInfoTitle, tr(L"Горячие клавиши не включены", L"Hotkeys are not active"), _TRUNCATE);
    wcsncpy_s(notice.szInfo, describeHotkeyError(hotkeyResult).c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &notice);
}
INT_PTR CALLBACK hotkeyDialogProc(HWND dialog, UINT msg, WPARAM w, LPARAM l) {
    INT_PTR themed = 0;
    if(quiet::ui::message(dialog,msg,w,l,themed)) return themed ? themed : TRUE;
    if (msg == WM_INITDIALOG) {
        quiet::ui::prepare(dialog);
        quiet::ui::apply(dialog);
        hotkeyDialog = dialog;
        SetDlgItemTextW(dialog,IDC_UI_TITLE,tr(L"Горячие клавиши",L"Hotkeys"));
        SetWindowTextW(dialog, tr(L"S-XMTray — горячие клавиши", L"S-XMTray — hotkeys"));
        SetDlgItemTextW(dialog, IDC_HOTKEY_ENABLE, tr(L"Включить глобальные горячие клавиши", L"Enable global hotkeys"));
        CheckDlgButton(dialog, IDC_HOTKEY_ENABLE, hotkeyConfig.enabled ? BST_CHECKED : BST_UNCHECKED);
        for (int i = 0; i < 3; ++i) {
            SetDlgItemTextW(dialog, IDC_LABEL_ANC + i, modeText(hotkeyModes[i]));
            SetDlgItemTextW(dialog, IDC_CLEAR_ANC + i, tr(L"Очистить", L"Clear"));
            // Accept input as typed; validate explicitly on Save rather than silently rewriting it.
            SendDlgItemMessageW(dialog, IDC_HOTKEY_ANC + i, HKM_SETRULES, 0, 0);
            SendDlgItemMessageW(dialog, IDC_HOTKEY_ANC + i, HKM_SETHOTKEY, hotkeyConfig.keys[i], 0);
            SetPropW(GetDlgItem(dialog,IDC_HOTKEY_ANC+i),L"QuietHint",
                reinterpret_cast<HANDLE>(const_cast<wchar_t*>(tr(L"Нажмите сочетание…",L"Press a shortcut…"))));
        }
        SetDlgItemTextW(dialog, IDC_HOTKEY_HINT, tr(
            L"Ctrl/Alt + клавиша; Shift — по желанию.\nВо время настройки хоткеи приостановлены.",
            L"Ctrl/Alt + a key; Shift is optional.\nHotkeys are paused while editing."));
        SetDlgItemTextW(dialog, IDC_HOTKEY_ERROR, describeHotkeyError(hotkeyResult).c_str());
        SetDlgItemTextW(dialog, IDOK, tr(L"Сохранить", L"Save"));
        SetDlgItemTextW(dialog, IDCANCEL, tr(L"Отмена", L"Cancel"));
        return TRUE;
    }
    if (msg == WM_CLOSE) { EndDialog(dialog, IDCANCEL); return TRUE; }
    if (msg == WM_COMMAND) {
        const int id = LOWORD(w);
        if (id >= IDC_CLEAR_ANC && id <= IDC_CLEAR_OFF) {
            SendDlgItemMessageW(dialog, IDC_HOTKEY_ANC + id - IDC_CLEAR_ANC, HKM_SETHOTKEY, 0, 0);
            SetDlgItemTextW(dialog, IDC_HOTKEY_ERROR, L""); return TRUE;
        }
        if (id == IDCANCEL || id==IDC_UI_CLOSE) { EndDialog(dialog, IDCANCEL); return TRUE; }
        if (id == IDOK) {
            quiet::HotkeyConfig candidate;
            candidate.enabled = IsDlgButtonChecked(dialog, IDC_HOTKEY_ENABLE) == BST_CHECKED;
            for (int i = 0; i < 3; ++i) {
                auto raw = static_cast<uint32_t>(SendDlgItemMessageW(dialog, IDC_HOTKEY_ANC + i, HKM_GETHOTKEY, 0, 0));
                candidate.keys[i] = (raw & 0xff) ? raw : 0;
            }
            auto result = hotkeyRegistration->apply(candidate);
            if (!result) {
                SetDlgItemTextW(dialog, IDC_HOTKEY_ERROR, describeHotkeyError(result).c_str());
                if (result.index >= 0) SetFocus(GetDlgItem(dialog, IDC_HOTKEY_ANC + result.index));
                return TRUE;
            }
            if (!saveHotkeys(candidate)) {
                hotkeyRegistration->clear();
                SetDlgItemTextW(dialog, IDC_HOTKEY_ERROR, tr(L"Не удалось сохранить настройки. Изменения не применены.", L"Could not save settings. Changes were not applied."));
                return TRUE;
            }
            hotkeyConfig = candidate; hotkeyResult = {};
            EndDialog(dialog, IDOK); return TRUE;
        }
    }
    return FALSE;
}
void showHotkeys() {
    if(eqDialog) { SetForegroundWindow(eqDialog); return; }
    if (hotkeyDialog) { SetForegroundWindow(hotkeyDialog); return; }
    hotkeyRegistration->clear();
    INT_PTR result = DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_HOTKEYS), window, hotkeyDialogProc, 0);
    hotkeyDialog = nullptr;
    if (result != IDOK) {
        hotkeyResult = hotkeyRegistration->apply(hotkeyConfig);
        notifyHotkeyFailure();
        if (result == -1) MessageBoxW(window, tr(L"Не удалось открыть настройки горячих клавиш.", L"Could not open hotkey settings."), L"S-XMTray", MB_OK | MB_ICONERROR);
    }
}
View snapshot() { std::lock_guard<std::mutex> lock(mutex); return view; }
void publish(bool connected, bool busy, sony::Mode mode, const std::wstring& error = L"") {
    { std::lock_guard<std::mutex> lock(mutex);
      view.connected=connected; view.busy=busy; view.mode=mode; view.error=error;
      if(!connected) view.features={}; }
    PostMessageW(window, StateMessage, 0, 0);
}
std::wstring widen(const char* message) {
    int n = MultiByteToWideChar(CP_UTF8, 0, message, -1, nullptr, 0);
    if (n <= 0) return L"Unknown error";
    std::wstring s(n, L'\0'); MultiByteToWideChar(CP_UTF8, 0, message, -1, s.data(), n);
    s.pop_back(); return s;
}
void runWorker() {
    try {
        sony::Winsock winsock;
        sony::Session session;
        session.cancelled = [] { return stopping.load(); };
        session.changed = [](const sony::NoiseState& state) {
            { std::lock_guard<std::mutex> lock(mutex); view.mode = state.mode; }
            PostMessageW(window, StateMessage, 0, 0);
        };
        session.featuresChanged = [](const sony::FeatureState& features) {
            { std::lock_guard<std::mutex> lock(mutex); view.features=features; }
            PostMessageW(window, StateMessage, 0, 0);
        };
        bool connected = false;
        auto nextRefresh = std::chrono::steady_clock::now();
        while (!stopping) {
            try {
                if (!connected) {
                    publish(false, true, sony::Mode::Unknown);
                    session.connectDevice(); connected = true;
                    publish(true, false, session.state.mode);
                    nextRefresh = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                }
                int action,value; sony::EqState bands;
                { std::lock_guard<std::mutex> lock(mutex); action = pending; value=pendingValue; bands=pendingBands; pending = 0; }
                if (action == Reconnect) { session.close(); connected = false; continue; }
                if (action) {
                    publish(true, true, session.state.mode);
                    try {
                        if(action==Anc || action==Ambient || action==Off)
                            session.setMode(action == Anc ? sony::Mode::Anc : action == Ambient ? sony::Mode::Ambient : sony::Mode::Off);
                        else if(action==Speak) session.setSpeak(value!=0);
                        else if(action==Sidetone) session.setSidetone(value!=0);
                        else if(action==EqPresetAction) session.setEqPreset(uint8_t(value));
                        else if(action==EqBandsAction) session.setEqBands(bands);
                        publish(true, false, session.state.mode);
                    } catch(const sony::FeatureError& e) { publish(true,false,session.state.mode,widen(e.what())); }
                }
                session.poll(250);
                if (std::chrono::steady_clock::now() >= nextRefresh) {
                    session.refresh();
                    session.refreshFeatures();
                    nextRefresh = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                }
            } catch (const std::exception& e) {
                session.close(); connected = false;
                publish(false, false, sony::Mode::Unknown, widen(e.what()));
                std::unique_lock<std::mutex> lock(mutex);
                // Never replay a stale mode command after a disconnection.
                pending = 0;
                wake.wait_for(lock, std::chrono::seconds(15), [] { return stopping || pending == Reconnect; });
                pending = 0;
            }
        }
    } catch (const std::exception& e) {
        publish(false, false, sony::Mode::Unknown, widen(e.what()));
    }
}
void request(int action,int value=0,const sony::EqState& bands={}) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (action != Reconnect && (!view.connected || view.busy)) return;
        pending = action; pendingValue=value; pendingBands=bands; view.busy = true; view.error.clear();
    }
    wake.notify_one(); PostMessageW(window, StateMessage, 0, 0);
}
bool customPreset(int id) { return id==0xa0 || id==0xa1 || id==0xa2; }
std::wstring presetName(uint8_t id) {
    switch(id) {
        case 0: return tr(L"Выключен",L"Off");
        case 0x10: return tr(L"Яркий",L"Bright");
        case 0x11: return tr(L"Энергичный",L"Excited");
        case 0x12: return tr(L"Мягкий",L"Mellow");
        case 0x13: return tr(L"Расслабленный",L"Relaxed");
        case 0x14: return tr(L"Вокал",L"Vocal");
        case 0x15: return tr(L"Высокие частоты",L"Treble boost");
        case 0x16: return tr(L"Низкие частоты",L"Bass boost");
        case 0x17: return tr(L"Речь",L"Speech");
        case 0xa0: return tr(L"Ручной (Custom)",L"Manual (Custom)");
        case 0xa1: return L"Custom 1";
        case 0xa2: return L"Custom 2";
        default: return std::wstring(tr(L"Пресет ",L"Preset "))+std::to_wstring(id);
    }
}
int selectedPreset(HWND dialog) {
    auto index=SendDlgItemMessageW(dialog,IDC_EQ_PRESET,CB_GETCURSEL,0,0);
    return index==CB_ERR ? -1 : static_cast<int>(SendDlgItemMessageW(dialog,IDC_EQ_PRESET,CB_GETITEMDATA,index,0));
}
void eqValues(HWND dialog) {
    for(int i=0;i<6;++i) {
        int value=10-static_cast<int>(SendDlgItemMessageW(dialog,IDC_EQ_SLIDER_FIRST+i,TBM_GETPOS,0,0));
        std::wstring text=(value>0 ? L"+" : L"")+std::to_wstring(value);
        SetDlgItemTextW(dialog,IDC_EQ_VALUE_FIRST+i,text.c_str());
    }
}
void updateEqDialog(HWND dialog,bool reset=false) {
    auto current=snapshot();
    auto count=SendDlgItemMessageW(dialog,IDC_EQ_PRESET,CB_GETCOUNT,0,0);
    bool rebuild=count!=static_cast<LRESULT>(current.features.eqCapabilities.presets.size());
    if(!rebuild) for(int i=0;i<count;++i)
        if(SendDlgItemMessageW(dialog,IDC_EQ_PRESET,CB_GETITEMDATA,i,0)!=current.features.eqCapabilities.presets[i]) rebuild=true;
    if(rebuild) {
        SendDlgItemMessageW(dialog,IDC_EQ_PRESET,CB_RESETCONTENT,0,0);
        for(auto preset:current.features.eqCapabilities.presets) {
            auto name=presetName(preset);
            auto index=SendDlgItemMessageW(dialog,IDC_EQ_PRESET,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));
            if(index>=0) SendDlgItemMessageW(dialog,IDC_EQ_PRESET,CB_SETITEMDATA,index,preset);
        }
        eqDirty=eqDraftSelection=false; reset=true;
    }
    if(eqApplying && !current.busy) { eqApplying=false; eqDirty=false; eqDraftSelection=false; reset=true; }
    bool ready=current.connected && current.features.equalizer && current.features.eqAvailable.value_or(false);
    if((reset || (!eqDirty && !eqDraftSelection)) && current.features.equalizer) {
        int n=static_cast<int>(SendDlgItemMessageW(dialog,IDC_EQ_PRESET,CB_GETCOUNT,0,0));
        for(int i=0;i<n;++i) if(SendDlgItemMessageW(dialog,IDC_EQ_PRESET,CB_GETITEMDATA,i,0)==current.features.equalizer->preset)
            SendDlgItemMessageW(dialog,IDC_EQ_PRESET,CB_SETCURSEL,i,0);
        for(int i=0;i<6;++i) SendDlgItemMessageW(dialog,IDC_EQ_SLIDER_FIRST+i,TBM_SETPOS,TRUE,20-current.features.equalizer->bands[i]);
        eqValues(dialog);
    }
    int selected=selectedPreset(dialog);
    bool manual=ready && !current.busy && customPreset(selected) && selected==current.features.equalizer->preset;
    for(int i=0;i<6;++i) EnableWindow(GetDlgItem(dialog,IDC_EQ_SLIDER_FIRST+i),manual);
    EnableWindow(GetDlgItem(dialog,IDC_EQ_RESET),manual);
    EnableWindow(GetDlgItem(dialog,IDC_EQ_PRESET),ready && !current.busy);
    EnableWindow(GetDlgItem(dialog,IDOK),ready && !current.busy && selected>=0 &&
        (eqDirty || selected!=current.features.equalizer->preset));
    std::wstring status=!current.connected ? tr(L"Наушники не подключены.",L"Headphones disconnected.") :
        current.busy ? tr(L"Применение / обновление…",L"Applying / updating…") :
        !current.error.empty() ? current.error :
        !ready ? tr(L"Эквалайзер сейчас недоступен.",L"Equalizer is currently unavailable.") :
        (eqDirty || (eqDraftSelection && current.features.equalizer && selected!=current.features.equalizer->preset)) ? tr(L"Изменения ещё не применены.",L"Changes have not been applied.") :
        tr(L"Показаны настройки, подтверждённые наушниками.",L"Showing settings confirmed by the headphones.");
    SetDlgItemTextW(dialog,IDC_EQ_STATUS,status.c_str());
}
INT_PTR CALLBACK equalizerDialogProc(HWND dialog,UINT msg,WPARAM w,LPARAM l) {
    INT_PTR themed = 0;
    if(quiet::ui::message(dialog,msg,w,l,themed)) return themed ? themed : TRUE;
    if(msg==WM_INITDIALOG) {
        quiet::ui::prepare(dialog);
        quiet::ui::apply(dialog);
        eqDialog=dialog; eqDirty=eqApplying=eqDraftSelection=false;
        SetDlgItemTextW(dialog,IDC_UI_TITLE,tr(L"Эквалайзер",L"Equalizer"));
        SetWindowTextW(dialog,tr(L"S-XMTray — эквалайзер",L"S-XMTray — equalizer"));
        SetDlgItemTextW(dialog,IDC_EQ_PRESET_LABEL,tr(L"Пресет",L"Preset"));
        SetDlgItemTextW(dialog,IDC_EQ_HINT,tr(
            L"Для ручной настройки примените Custom.\n«Применить» сохраняет полосы в выбранный пресет.",
            L"Apply a Custom preset to edit bands.\nApply saves changes to that preset."));
        SetDlgItemTextW(dialog,IDC_EQ_RESET,tr(L"Сбросить полосы",L"Reset bands"));
        SetDlgItemTextW(dialog,IDOK,tr(L"Применить",L"Apply"));
        SetDlgItemTextW(dialog,IDCANCEL,tr(L"Закрыть",L"Close"));
        auto current=snapshot();
        for(auto preset:current.features.eqCapabilities.presets) {
            auto name=presetName(preset);
            auto i=SendDlgItemMessageW(dialog,IDC_EQ_PRESET,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));
            if(i>=0) SendDlgItemMessageW(dialog,IDC_EQ_PRESET,CB_SETITEMDATA,i,preset);
        }
        for(int i=0;i<6;++i) {
            SendDlgItemMessageW(dialog,IDC_EQ_SLIDER_FIRST+i,TBM_SETRANGE,TRUE,MAKELPARAM(0,20));
            SendDlgItemMessageW(dialog,IDC_EQ_SLIDER_FIRST+i,TBM_SETTICFREQ,10,0);
            SendDlgItemMessageW(dialog,IDC_EQ_SLIDER_FIRST+i,TBM_SETPAGESIZE,0,1);
        }
        updateEqDialog(dialog,true); return TRUE;
    }
    if(msg==WM_CLOSE) { EndDialog(dialog,IDCANCEL);return TRUE; }
    if(msg==EqStateMessage) { updateEqDialog(dialog);return TRUE; }
    if(msg==WM_VSCROLL) { eqDirty=true;eqValues(dialog);updateEqDialog(dialog);return TRUE; }
    if(msg==WM_COMMAND) {
        int id=LOWORD(w);
        if(id==IDCANCEL || id==IDC_UI_CLOSE) { EndDialog(dialog,IDCANCEL);return TRUE; }
        if(id==IDC_EQ_PRESET && HIWORD(w)==CBN_SELCHANGE) {
            eqDirty=false;eqDraftSelection=true;updateEqDialog(dialog);return TRUE;
        }
        if(id==IDC_EQ_RESET) {
            for(int i=0;i<6;++i) SendDlgItemMessageW(dialog,IDC_EQ_SLIDER_FIRST+i,TBM_SETPOS,TRUE,10);
            eqDirty=true;eqValues(dialog);updateEqDialog(dialog);return TRUE;
        }
        if(id==IDOK) {
            auto current=snapshot();int preset=selectedPreset(dialog);
            if(!current.connected || current.busy || !current.features.eqAvailable.value_or(false) || preset<0) return TRUE;
            if(eqDirty) {
                sony::EqState target;target.preset=uint8_t(preset);
                for(int i=0;i<6;++i) target.bands[i]=uint8_t(20-SendDlgItemMessageW(dialog,IDC_EQ_SLIDER_FIRST+i,TBM_GETPOS,0,0));
                request(EqBandsAction,0,target);
            } else request(EqPresetAction,preset);
            eqApplying=true;updateEqDialog(dialog);return TRUE;
        }
    }
    return FALSE;
}
void showEqualizer() {
    if(eqDialog) { SetForegroundWindow(eqDialog);return; }
    if(hotkeyDialog) { SetForegroundWindow(hotkeyDialog);return; }
    DialogBoxParamW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(IDD_EQUALIZER),window,equalizerDialogProc,0);
    eqDialog=nullptr;
}
std::wstring batteryText(const sony::Battery& battery,std::optional<bool> connected={}) {
    if(connected && !*connected) return tr(L"нет связи",L"disconnected");
    if(!battery.percent || battery.received==std::chrono::steady_clock::time_point{} ||
        std::chrono::steady_clock::now()-battery.received>std::chrono::seconds(90)) return L"—";
    return std::to_wstring(*battery.percent)+L"%"+(battery.charging==1 ? tr(L" (зарядка)",L" (charging)") : L"");
}
std::wstring startupCommand() {
    wchar_t path[32768];
    DWORD n = GetModuleFileNameW(nullptr, path, 32768);
    if (!n || n >= 32768) throw std::runtime_error("Cannot locate executable");
    return L"\"" + std::wstring(path, n) + L"\"";
}
bool startupEnabled() {
    wchar_t data[32768]; DWORD n = sizeof(data);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        L"QuietTray", RRF_RT_REG_SZ, nullptr, data, &n) != ERROR_SUCCESS) return false;
    return startupCommand() == data;
}
void toggleStartup() {
    const bool enabled = startupEnabled();
    HKEY key = nullptr;
    LSTATUS result = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (result == ERROR_SUCCESS) {
        auto command = startupCommand();
        result = enabled ? RegDeleteValueW(key, L"QuietTray") :
            RegSetValueExW(key, L"QuietTray", 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
    }
    if (result != ERROR_SUCCESS) MessageBoxW(window,
        tr(L"Не удалось изменить автозапуск.", L"Could not change startup setting."), L"S-XMTray", MB_OK | MB_ICONERROR);
}
void updateTray(bool add = false) {
    const View state = snapshot();
    const wchar_t* status = state.busy ? tr(L"Подключение / обновление…", L"Connecting / updating…") :
        state.connected ? modeText(state.mode) : tr(L"Наушники недоступны", L"Headphones unavailable");
    std::wstring tip = L"S-XMTray · WF-1000XM5\n"; tip += status;
    if(state.connected) tip+=std::wstring(tr(L"\nЛ: ",L"\nL: "))+batteryText(state.features.left,state.features.leftConnected)+
        tr(L" · П: ",L" · R: ")+batteryText(state.features.right,state.features.rightConnected)+
        tr(L"\nКейс (последние данные): ",L"\nCase (last reported): ")+batteryText(state.features.cradle);
    wcsncpy_s(tray.szTip, tip.c_str(), _TRUNCATE);
    int icon = !state.connected ? 1 : state.mode == sony::Mode::Anc ? 2 : state.mode == sony::Mode::Ambient ? 3 : 4;
    tray.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(icon));
    Shell_NotifyIconW(add ? NIM_ADD : NIM_MODIFY, &tray);
    if (add) { tray.uVersion = NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION, &tray); }
}
void showAbout() {
    auto state = snapshot();
    std::wstring text = tr(
        L"S-XMTray 0.5.2\nУправление WF-1000XM5\n\nНеофициальная утилита, не связанная с Sony.\n\n",
        L"S-XMTray 0.5.2\nWF-1000XM5 control\n\nUnofficial utility, not affiliated with Sony.\n\n");
    text += state.connected ? modeText(state.mode) : tr(L"Нет соединения с управлением наушников.", L"Headphone control is disconnected.");
    if(!state.features.firmware.empty()) text+=std::wstring(tr(L"\nПрошивка: ",L"\nFirmware: "))+widen(state.features.firmware.c_str());
    if(!state.features.warning.empty()) text+=std::wstring(tr(L"\nДополнительные функции: ",L"\nOptional features: "))+widen(state.features.warning.c_str());
    if (!state.error.empty()) {
        text += tr(L"\n\nПодключите наушники в Windows. Если не помогает, закройте Sound Connect на телефоне и нажмите «Переподключить».\n\nДиагностика: ",
                   L"\n\nConnect the headphones in Windows. If needed, close Sound Connect on your phone and choose Reconnect.\n\nDiagnostics: ");
        text += state.error;
    }
    MessageBoxW(window, text.c_str(), tr(L"S-XMTray — сведения", L"S-XMTray — about"), MB_OK | MB_ICONINFORMATION);
}
void showMenu() {
    if (hotkeyDialog) { SetForegroundWindow(hotkeyDialog); return; }
    if (eqDialog) { SetForegroundWindow(eqDialog); return; }
    auto state = snapshot();
    HMENU menu = CreatePopupMenu(), langs = CreatePopupMenu(), themes = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"S-XMTray · WF-1000XM5");
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, state.busy ? tr(L"Подключение / обновление…", L"Connecting / updating…") :
        state.connected ? tr(L"Подключено", L"Connected") : tr(L"Наушники недоступны", L"Headphones unavailable"));
    if(state.connected) {
        auto earbuds=std::wstring(tr(L"Левый: ",L"Left: "))+batteryText(state.features.left,state.features.leftConnected)+
            tr(L"   Правый: ",L"   Right: ")+batteryText(state.features.right,state.features.rightConnected);
        auto cradle=std::wstring(tr(L"Кейс: ",L"Case: "))+batteryText(state.features.cradle)+tr(L" (последние данные)",L" (last reported)");
        AppendMenuW(menu,MF_STRING|MF_GRAYED,0,earbuds.c_str());
        AppendMenuW(menu,MF_STRING|MF_GRAYED,0,cradle.c_str());
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    UINT flags = MF_STRING | ((!state.connected || state.busy) ? MF_GRAYED : 0);
    for (int i = 0; i < 3; ++i) {
        std::wstring label = modeText(hotkeyModes[i]);
        if (hotkeyConfig.enabled && hotkeyResult && hotkeyConfig.keys[i]) label += L"\t" + hotkeyText(hotkeyConfig.keys[i]);
        AppendMenuW(menu, flags | (state.mode == hotkeyModes[i] ? MF_CHECKED : 0), hotkeyActions[i], label.c_str());
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    auto toggle=[&](UINT id,const wchar_t* name,std::optional<bool> value) {
        std::wstring label=name;
        if(!value) label+=tr(L" — недоступно",L" — unavailable");
        AppendMenuW(menu,flags|(!value?MF_GRAYED:0)|(value.value_or(false)?MF_CHECKED:0),id,label.c_str());
    };
    toggle(Speak,L"Speak-to-Chat",state.features.speak);
    toggle(Sidetone,tr(L"Слышать свой голос при звонках",L"Hear my voice during calls"),state.features.sidetone);
    AppendMenuW(menu,flags|(!state.features.equalizer || !state.features.eqAvailable.value_or(false) ||
        state.features.eqCapabilities.presets.empty()?MF_GRAYED:0),Equalizer,tr(L"Эквалайзер и Clear Bass…",L"Equalizer and Clear Bass…"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (state.busy ? MF_GRAYED : 0), Reconnect, tr(L"Переподключить", L"Reconnect"));
    AppendMenuW(langs, MF_STRING | (language == 0 ? MF_CHECKED : 0), LangAuto, tr(L"Системный язык", L"System language"));
    AppendMenuW(langs, MF_STRING | (language == 1 ? MF_CHECKED : 0), LangRu, L"Русский");
    AppendMenuW(langs, MF_STRING | (language == 2 ? MF_CHECKED : 0), LangEn, L"English");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(langs), tr(L"Язык", L"Language"));
    AppendMenuW(themes,MF_STRING|(quiet::ui::preference==0?MF_CHECKED:0),ThemeAuto,tr(L"Как в Windows",L"Follow Windows"));
    AppendMenuW(themes,MF_STRING|(quiet::ui::preference==1?MF_CHECKED:0),ThemeLight,tr(L"Светлая",L"Light"));
    AppendMenuW(themes,MF_STRING|(quiet::ui::preference==2?MF_CHECKED:0),ThemeDark,tr(L"Тёмная",L"Dark"));
    AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(themes),tr(L"Тема оформления",L"Appearance"));
    AppendMenuW(menu, MF_STRING, Hotkeys, hotkeyResult ? tr(L"Горячие клавиши…", L"Hotkeys…") : tr(L"Горячие клавиши… (ошибка)", L"Hotkeys… (error)"));
    AppendMenuW(menu, MF_STRING | (startupEnabled() ? MF_CHECKED : 0), Startup, tr(L"Запускать с Windows", L"Start with Windows"));
    AppendMenuW(menu, MF_STRING, About, tr(L"Сведения и диагностика", L"About and diagnostics"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, Exit, tr(L"Выход", L"Exit"));
    POINT p; GetCursorPos(&p);
    SetForegroundWindow(window);
    UINT id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, window, nullptr);
    PostMessageW(window, WM_NULL, 0, 0); DestroyMenu(menu);
    if (id == Anc || id == Ambient || id == Off || id == Reconnect) request(id);
    else if (id == Exit) DestroyWindow(window);
    else if (id == About) showAbout();
    else if (id == Startup) toggleStartup();
    else if (id == Hotkeys) showHotkeys();
    else if (id == Speak && state.features.speak) request(Speak,!*state.features.speak);
    else if (id == Sidetone && state.features.sidetone) request(Sidetone,!*state.features.sidetone);
    else if (id == Equalizer) showEqualizer();
    else if (id >= ThemeAuto && id <= ThemeDark) {
        int candidate=id-ThemeAuto;
        if(WritePrivateProfileStringW(L"General",L"Theme",std::to_wstring(candidate).c_str(),settingsFile.c_str())) {
            quiet::ui::preference=candidate;applyTheme();showPanel();
        } else MessageBoxW(window,tr(L"Не удалось сохранить тему.",L"Could not save theme."),L"S-XMTray",MB_OK|MB_ICONERROR);
    }
    else if (id >= LangAuto && id <= LangEn) {
        language = id - LangAuto;
        if (!WritePrivateProfileStringW(L"General", L"Language", std::to_wstring(language).c_str(), settingsFile.c_str()))
            MessageBoxW(window, tr(L"Не удалось сохранить язык.", L"Could not save language."), L"S-XMTray", MB_OK | MB_ICONERROR);
        updateTray(); updatePanel();
    }
}
void updateSettings() {
    if(!settingsDialog) return;
    HWND dialog=settingsDialog;auto state=snapshot();
    SetWindowTextW(dialog,tr(L"S-XMTray — настройки",L"S-XMTray — settings"));
    SetDlgItemTextW(dialog,IDC_UI_TITLE,tr(L"Настройки",L"Settings"));
    SetDlgItemTextW(dialog,IDC_UI_CLOSE,tr(L"Закрыть",L"Close"));
    SetDlgItemTextW(dialog,IDC_SETTINGS_DEVICE,tr(L"НАУШНИКИ",L"HEADPHONES"));
    SetDlgItemTextW(dialog,Speak,L"Speak-to-Chat");
    SetDlgItemTextW(dialog,Sidetone,tr(L"Свой голос при звонках",L"Hear my voice during calls"));
    SetDlgItemTextW(dialog,Equalizer,tr(L"Эквалайзер и Clear Bass",L"Equalizer and Clear Bass"));
    SetDlgItemTextW(dialog,Hotkeys,tr(L"Горячие клавиши",L"Hotkeys"));
    SetDlgItemTextW(dialog,IDC_SETTINGS_APPEARANCE,tr(L"ОФОРМЛЕНИЕ",L"APPEARANCE"));
    SetDlgItemTextW(dialog,ThemeAuto,tr(L"Как в Windows",L"System"));
    SetDlgItemTextW(dialog,ThemeLight,tr(L"Светлая",L"Light"));
    SetDlgItemTextW(dialog,ThemeDark,tr(L"Тёмная",L"Dark"));
    SetDlgItemTextW(dialog,IDC_SETTINGS_LANGUAGE,tr(L"ЯЗЫК",L"LANGUAGE"));
    SetDlgItemTextW(dialog,LangAuto,tr(L"Системный",L"System"));
    SetDlgItemTextW(dialog,LangRu,L"Русский");SetDlgItemTextW(dialog,LangEn,L"English");
    SetDlgItemTextW(dialog,Startup,tr(L"Запускать с Windows",L"Start with Windows"));
    SetDlgItemTextW(dialog,Reconnect,tr(L"Переподключить",L"Reconnect"));
    SetDlgItemTextW(dialog,About,tr(L"Диагностика",L"Diagnostics"));
    SetDlgItemTextW(dialog,Exit,tr(L"Выход",L"Exit"));
    for(int i=0;i<3;++i) {
        for(auto entry : {std::pair<int,int>{ThemeAuto,quiet::ui::preference}, {LangAuto,language}}) {
            HWND control=GetDlgItem(dialog,entry.first+i);
            if(entry.second==i) SetPropW(control,L"QuietSelected",reinterpret_cast<HANDLE>(1));
            else RemovePropW(control,L"QuietSelected");
            InvalidateRect(control,nullptr,TRUE);
        }
    }
    CheckDlgButton(dialog,Startup,startupEnabled()?BST_CHECKED:BST_UNCHECKED);
    CheckDlgButton(dialog,Speak,state.features.speak.value_or(false)?BST_CHECKED:BST_UNCHECKED);
    CheckDlgButton(dialog,Sidetone,state.features.sidetone.value_or(false)?BST_CHECKED:BST_UNCHECKED);
    EnableWindow(GetDlgItem(dialog,Speak),state.connected&&!state.busy&&state.features.speak.has_value());
    EnableWindow(GetDlgItem(dialog,Sidetone),state.connected&&!state.busy&&state.features.sidetone.has_value());
    EnableWindow(GetDlgItem(dialog,Equalizer),state.connected&&!state.busy&&state.features.equalizer.has_value()&&state.features.eqAvailable.value_or(false));
    EnableWindow(GetDlgItem(dialog,Reconnect),!state.busy);
    SetDlgItemTextW(dialog,IDC_SETTINGS_STATUS,state.busy?tr(L"Подключение / обновление…",L"Connecting / updating…"):
        !state.error.empty()?tr(L"Ошибка · подробности в диагностике",L"Error · see diagnostics"):
        state.connected?tr(L"WF-1000XM5 · подключено",L"WF-1000XM5 · connected"):tr(L"Наушники недоступны",L"Headphones unavailable"));
}
INT_PTR CALLBACK settingsProc(HWND dialog,UINT msg,WPARAM w,LPARAM l) {
    INT_PTR themed=0;if(quiet::ui::message(dialog,msg,w,l,themed)) return themed?themed:TRUE;
    if(msg==WM_INITDIALOG) { settingsDialog=dialog;quiet::ui::prepare(dialog);quiet::ui::apply(dialog);updateSettings();return TRUE; }
    if(msg==WM_CLOSE) { ShowWindow(dialog,SW_HIDE);return TRUE; }
    if(msg==WM_COMMAND) {
        int id=LOWORD(w);auto state=snapshot();
        if(id==IDCANCEL || id==IDC_UI_CLOSE) { ShowWindow(dialog,SW_HIDE);return TRUE; }
        if(id==Speak && state.features.speak) request(id,!*state.features.speak);
        else if(id==Sidetone && state.features.sidetone) request(id,!*state.features.sidetone);
        else if(id==Reconnect) request(id);
        else if(id==Startup) toggleStartup();
        else if(id==Exit) { DestroyWindow(window);return TRUE; }
        else if(id==Hotkeys || id==Equalizer || id==About) {
            ShowWindow(dialog,SW_HIDE);
            if(id==Hotkeys) showHotkeys();else if(id==Equalizer) showEqualizer();else showAbout();
            showSettings();
        } else if(id>=ThemeAuto && id<=ThemeDark) {
            int value=id-ThemeAuto;
            if(WritePrivateProfileStringW(L"General",L"Theme",std::to_wstring(value).c_str(),settingsFile.c_str())) {
                quiet::ui::preference=value;applyTheme();
            } else MessageBoxW(dialog,tr(L"Не удалось сохранить тему.",L"Could not save theme."),L"S-XMTray",MB_OK|MB_ICONERROR);
        } else if(id>=LangAuto && id<=LangEn) {
            int value=id-LangAuto;
            if(WritePrivateProfileStringW(L"General",L"Language",std::to_wstring(value).c_str(),settingsFile.c_str())) {
                language=value;updateTray();updatePanel();
            } else MessageBoxW(dialog,tr(L"Не удалось сохранить язык.",L"Could not save language."),L"S-XMTray",MB_OK|MB_ICONERROR);
        }
        updateSettings();return TRUE;
    }
    return FALSE;
}
void showSettings() {
    if(hotkeyDialog) { SetForegroundWindow(hotkeyDialog);return; }
    if(eqDialog) { SetForegroundWindow(eqDialog);return; }
    if(panel) ShowWindow(panel,SW_HIDE);
    if(!settingsDialog) CreateDialogParamW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(IDD_SETTINGS),window,settingsProc,0);
    if(!settingsDialog) { showMenu();return; }
    updateSettings();ShowWindow(settingsDialog,SW_SHOW);SetForegroundWindow(settingsDialog);
}
void updatePanel() {
    if(!panel) return;
    auto state=snapshot();
    const wchar_t* status=state.busy?tr(L"Подключение / обновление…",L"Connecting / updating…"):
        !state.error.empty()?tr(L"Ошибка · подробнее в диагностике",L"Error · see diagnostics"):
        state.connected?tr(L"Подключено по Bluetooth",L"Connected via Bluetooth"):tr(L"Наушники недоступны",L"Headphones unavailable");
    SetDlgItemTextW(panel,IDC_PANEL_STATUS,status);
    auto compact=[](const sony::Battery& battery,std::optional<bool> connected=std::nullopt) {
        auto value=batteryText(battery,connected);auto pos=value.find(L" (");
        if(connected && !*connected) return std::wstring(L"—");
        return pos==std::wstring::npos?value:value.substr(0,pos);
    };
    auto buds=std::wstring(tr(L"Л: ",L"L: "))+compact(state.features.left,state.features.leftConnected);
    auto right=std::wstring(tr(L"П: ",L"R: "))+compact(state.features.right,state.features.rightConnected);
    auto cradle=std::wstring(tr(L"Кейс: ",L"Case: "))+compact(state.features.cradle);
    SetDlgItemTextW(panel,IDC_PANEL_BATTERY,buds.c_str());
    SetDlgItemTextW(panel,IDC_PANEL_CASE,cradle.c_str());
    SetDlgItemTextW(panel,IDC_PANEL_RIGHT,right.c_str());
    SetDlgItemTextW(panel,IDC_PANEL_NOTE,tr(L"Заряд кейса — последние данные",L"Case battery: last reported"));
    SetDlgItemTextW(panel,IDC_PANEL_NOISE,tr(L"УПРАВЛЕНИЕ ШУМОМ",L"NOISE CONTROL"));
    SetDlgItemTextW(panel,IDC_PANEL_SETTINGS,tr(L"Настройки",L"Settings"));
    SetDlgItemTextW(panel,Anc,tr(L"Шумодав",L"ANC"));
    SetDlgItemTextW(panel,Ambient,tr(L"Окружение",L"Ambient"));
    SetDlgItemTextW(panel,Off,tr(L"Выкл.",L"Off"));
    for(int i=0;i<3;++i) {
        HWND control=GetDlgItem(panel,hotkeyActions[i]);
        if(state.connected && state.mode==hotkeyModes[i]) SetPropW(control,L"QuietSelected",reinterpret_cast<HANDLE>(1));
        else RemovePropW(control,L"QuietSelected");
        EnableWindow(control,state.connected&&!state.busy);
        InvalidateRect(control,nullptr,TRUE);
    }
}
INT_PTR CALLBACK panelProc(HWND dialog,UINT msg,WPARAM w,LPARAM l) {
    INT_PTR themed=0;
    if(quiet::ui::message(dialog,msg,w,l,themed)) return themed?themed:TRUE;
    if(msg==WM_INITDIALOG) { panel=dialog;quiet::ui::prepare(dialog,true);quiet::ui::apply(dialog);updatePanel();return TRUE; }
    if(msg==WM_CLOSE || (msg==WM_COMMAND && (LOWORD(w)==IDCANCEL || LOWORD(w)==IDC_UI_CLOSE))) { ShowWindow(dialog,SW_HIDE);return TRUE; }
    if(msg==WM_ACTIVATE && LOWORD(w)==WA_INACTIVE) { ShowWindow(dialog,SW_HIDE);return TRUE; }
    if(msg==WM_COMMAND) {
        int id=LOWORD(w);
        if(id==Anc || id==Ambient || id==Off) { request(id);return TRUE; }
        if(id==IDC_PANEL_SETTINGS) {
            ShowWindow(dialog,SW_HIDE);PostMessageW(window,OpenSettingsMessage,0,0);return TRUE;
        }
    }
    return FALSE;
}
void showPanel() {
    if(settingsDialog && IsWindowVisible(settingsDialog)) { SetForegroundWindow(settingsDialog);return; }
    if(hotkeyDialog) { SetForegroundWindow(hotkeyDialog);return; }
    if(eqDialog) { SetForegroundWindow(eqDialog);return; }
    if(!panel) CreateDialogParamW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(IDD_PANEL),window,panelProc,0);
    if(!panel) { showMenu();return; }
    updatePanel();
    POINT p{};GetCursorPos(&p);
    NOTIFYICONIDENTIFIER icon{sizeof(icon)};icon.hWnd=window;icon.uID=tray.uID;
    RECT anchor{};
    if(SUCCEEDED(Shell_NotifyIconGetRect(&icon,&anchor))) p={anchor.right,anchor.top};
    MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromPoint(p,MONITOR_DEFAULTTONEAREST),&monitor);
    RECT rect{};GetWindowRect(panel,&rect);int width=rect.right-rect.left,height=rect.bottom-rect.top;
    int x=(std::max)(monitor.rcWork.left,(std::min)(p.x-width,monitor.rcWork.right-width));
    int y=(std::max)(monitor.rcWork.top,(std::min)(p.y-height-8,monitor.rcWork.bottom-height));
    SetWindowPos(panel,HWND_TOP,x,y,0,0,SWP_NOSIZE|SWP_SHOWWINDOW);
    SetForegroundWindow(panel);SetFocus(GetDlgItem(panel,IDC_PANEL_SETTINGS));
}
LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if (taskbarCreated && msg == taskbarCreated) { updateTray(true); return 0; }
    switch (msg) {
        case TrayMessage:
            if (LOWORD(l) == WM_CONTEXTMENU) showSettings();
            else if (LOWORD(l) == NIN_SELECT || LOWORD(l) == NIN_KEYSELECT) showPanel();
            return 0;
        case StateMessage: updateTray(); updatePanel(); updateSettings(); if(eqDialog) SendMessageW(eqDialog,EqStateMessage,0,0); return 0;
        case OpenSettingsMessage: showSettings();return 0;
        case OpenPanelMessage: showPanel();return 0;
        case WM_SETTINGCHANGE: applyTheme();return 0;
        case OpenMenuMessage: showMenu(); return 0;
        case OpenAboutMessage: showAbout(); return 0;
        case OpenHotkeysMessage: showHotkeys(); return 0;
        case OpenEqualizerMessage: showEqualizer(); return 0;
        case WM_HOTKEY:
            if (!hotkeyDialog && hotkeyRegistration && hotkeyRegistration->accepts(w, l, hotkeyConfig))
                request(hotkeyActions[w - quiet::HotkeyRegistration::FirstId]);
            return 0;
        case WM_CLOSE: DestroyWindow(hwnd); return 0;
        case WM_DESTROY:
            hotkeyRegistration.reset();
            stopping = true; wake.notify_all();
            if (worker.joinable()) worker.join();
            Shell_NotifyIconW(NIM_DELETE, &tray); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command, int) {
    // Keep the development-build identifiers and settings path for compatibility.
    HANDLE singleton = CreateMutexW(nullptr, FALSE, L"Local\\QuietTray-WF1000XM5");
    if (!singleton) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(L"QuietTrayWindow", L"QuietTray"))
            PostMessageW(existing, wcscmp(command, L"--about") == 0 ? OpenAboutMessage :
                wcscmp(command, L"--hotkeys") == 0 ? OpenHotkeysMessage :
                wcscmp(command, L"--equalizer") == 0 ? OpenEqualizerMessage :
                wcscmp(command,L"--settings")==0 ? OpenSettingsMessage :
                wcscmp(command,L"--menu")==0 ? OpenMenuMessage : OpenPanelMessage, 0, 0);
        CloseHandle(singleton); return 0;
    }
    PWSTR folder = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &folder))) {
        std::wstring dir = std::wstring(folder) + L"\\QuietTray"; CoTaskMemFree(folder);
        CreateDirectoryW(dir.c_str(), nullptr); settingsFile = dir + L"\\settings.ini";
    } else { CloseHandle(singleton); return 1; }
    language = GetPrivateProfileIntW(L"General", L"Language", 0, settingsFile.c_str());
    if (language < 0 || language > 2) language = 0;
    quiet::ui::preference=GetPrivateProfileIntW(L"General",L"Theme",0,settingsFile.c_str());
    if(quiet::ui::preference<0 || quiet::ui::preference>2) quiet::ui::preference=0;
    quiet::ui::refresh();
    hotkeyConfig.enabled = GetPrivateProfileIntW(L"Hotkeys", L"Enabled", 0, settingsFile.c_str()) == 1;
    for (int i = 0; i < 3; ++i) hotkeyConfig.keys[i] = GetPrivateProfileIntW(L"Hotkeys", hotkeyNames[i], 0, settingsFile.c_str());
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_HOTKEY_CLASS | ICC_BAR_CLASSES};
    if (!InitCommonControlsEx(&controls)) { CloseHandle(singleton); return 1; }
    taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    WNDCLASSW wc{}; wc.hInstance = instance; wc.lpfnWndProc = windowProc; wc.lpszClassName = L"QuietTrayWindow";
    RegisterClassW(&wc);
    window = CreateWindowExW(0, wc.lpszClassName, L"QuietTray", WS_OVERLAPPED, 0,0,0,0, nullptr,nullptr,instance,nullptr);
    if (!window) { CloseHandle(singleton); return 1; }
    tray.cbSize = sizeof(tray); tray.hWnd = window; tray.uID = 1;
    tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP; tray.uCallbackMessage = TrayMessage;
    updateTray(true);
    hotkeyRegistration = std::make_unique<quiet::HotkeyRegistration>(window);
    hotkeyResult = hotkeyRegistration->apply(hotkeyConfig);
    notifyHotkeyFailure();
    worker = std::thread(runWorker);
    if (wcscmp(command, L"--menu") == 0) PostMessageW(window, OpenMenuMessage, 0, 0);
    if (wcscmp(command, L"--about") == 0) PostMessageW(window, OpenAboutMessage, 0, 0);
    if (wcscmp(command, L"--hotkeys") == 0) PostMessageW(window, OpenHotkeysMessage, 0, 0);
    if (wcscmp(command, L"--equalizer") == 0) PostMessageW(window, OpenEqualizerMessage, 0, 0);
    if (wcscmp(command,L"--panel")==0) PostMessageW(window,OpenPanelMessage,0,0);
    if (wcscmp(command,L"--settings")==0) PostMessageW(window,OpenSettingsMessage,0,0);
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if(panel && IsWindowVisible(panel) && IsDialogMessageW(panel,&message)) continue;
        if(settingsDialog && IsWindowVisible(settingsDialog) && IsDialogMessageW(settingsDialog,&message)) continue;
        TranslateMessage(&message); DispatchMessageW(&message);
    }
    quiet::ui::cleanup();
    CloseHandle(singleton); return 0;
}
