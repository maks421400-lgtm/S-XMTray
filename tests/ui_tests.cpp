// Native UI regression/preview harness. Uses real dialogs and painting, but no
// Bluetooth session, worker, hotkey registration or writes to user preferences.
#define wWinMain quietTrayEntry
#include "../src/tray.cpp"
#undef wWinMain
#include <iostream>

bool hoverStaysIdle(HWND control) {
    ValidateRect(control,nullptr);
    for(int i=0;i<40;++i) SendMessageW(control,WM_MOUSEMOVE,0,MAKELPARAM(4+i%12,8));
    SendMessageW(control,WM_MOUSELEAVE,0,0);
    bool clean=GetUpdateRect(control,nullptr,FALSE)==FALSE;
    if(!clean) std::cerr<<"Hover repeatedly invalidates control "<<GetDlgCtrlID(control)<<"\n";
    return clean;
}
bool paintIsStable(HWND control) {
    RECT r{};GetClientRect(control,&r);HDC screen=GetDC(control),memory=CreateCompatibleDC(screen);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=r.right;info.bmiHeader.biHeight=-r.bottom;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* pixels=nullptr;HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    if(!bitmap || !memory) {
        if(bitmap) DeleteObject(bitmap);if(memory) DeleteDC(memory);ReleaseDC(control,screen);return false;
    }
    auto old=SelectObject(memory,bitmap);size_t bytes=static_cast<size_t>(r.right)*r.bottom*4;
    std::vector<BYTE> reference(bytes);bool stable=true;
    // Warm up GDI/GDI+ caches before measuring handles. Repeated frames must
    // fully overwrite arbitrary previous pixels and release their buffers.
    DWORD handles=0;
    for(int i=0;i<80;++i) {
        memset(pixels,i&1?0x00:0xff,bytes);
        SendMessageW(control,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(memory),PRF_CLIENT);
        GdiFlush();
        if(!i) { memcpy(reference.data(),pixels,bytes);handles=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS); }
        else if(memcmp(reference.data(),pixels,bytes)!=0) stable=false;
    }
    DWORD after=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    if(after>handles+2) { std::cerr<<"Paint buffers leak GDI objects\n";stable=false; }
    SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(control,screen);
    if(!stable) std::cerr<<"Unstable paint for control "<<GetDlgCtrlID(control)<<"\n";
    return stable;
}
bool savePreview(HWND dialog,const std::wstring& path) {
    RECT r{};GetClientRect(dialog,&r);HDC dc=GetDC(dialog),memory=CreateCompatibleDC(dc);
    HBITMAP bitmap=CreateCompatibleBitmap(dc,r.right,r.bottom);auto old=SelectObject(memory,bitmap);
    BOOL painted=PrintWindow(dialog,memory,PW_CLIENTONLY);
    // Some common controls omit subclass painting during parent WM_PRINT.
    // Exercise their actual WM_PRINTCLIENT path explicitly on the test canvas.
    for(HWND child=GetWindow(dialog,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)) {
        DWORD_PTR data=0;
        if(!GetWindowSubclass(child,quiet::ui::hotkeyPaint,1,&data) &&
            !GetWindowSubclass(child,quiet::ui::inputPaint,3,&data)) continue;
        RECT bounds{};GetWindowRect(child,&bounds);MapWindowPoints(nullptr,dialog,reinterpret_cast<POINT*>(&bounds),2);
        int saved=SaveDC(memory);SetViewportOrgEx(memory,bounds.left,bounds.top,nullptr);
        IntersectClipRect(memory,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top);
        SendMessageW(child,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(memory),PRF_CLIENT|PRF_ERASEBKGND);
        RestoreDC(memory,saved);
    }
    bool saved=false;
    if(painted) {
        Gdiplus::Bitmap output(bitmap,nullptr);UINT count=0,size=0;Gdiplus::GetImageEncodersSize(&count,&size);
        std::vector<BYTE> bytes(size);auto encoders=reinterpret_cast<Gdiplus::ImageCodecInfo*>(bytes.data());
        Gdiplus::GetImageEncoders(count,size,encoders);
        for(UINT i=0;i<count;++i) if(wcscmp(encoders[i].MimeType,L"image/png")==0)
            saved=output.Save(path.c_str(),&encoders[i].Clsid,nullptr)==Gdiplus::Ok;
    }
    SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(dialog,dc);return saved;
}
int wmain(int argc,wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    language=1;quiet::ui::refresh();
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_HOTKEY_CLASS|ICC_BAR_CLASSES};InitCommonControlsEx(&controls);
    WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"QuietTrayUiTest";
    RegisterClassW(&wc);window=CreateWindowW(wc.lpszClassName,L"UI test owner",0,0,0,0,0,nullptr,nullptr,wc.hInstance,nullptr);
    view.connected=true;view.mode=sony::Mode::Anc;
    view.features.left.percent=85;view.features.right.percent=60;view.features.cradle.percent=100;
    view.features.left.received=view.features.right.received=view.features.cradle.received=std::chrono::steady_clock::now();
    view.features.leftConnected=true;view.features.rightConnected=true;
    view.features.speak=false;view.features.sidetone=true;
    view.features.eqAvailable=true;view.features.equalizer=sony::EqState{};
    view.features.equalizer->preset=0xa0;
    view.features.equalizer->bands.fill(10);view.features.eqCapabilities.presets={0,0x10,0xa0,0xa1,0xa2};
    bool ok=true;
    struct Test { int id;DLGPROC proc;const wchar_t* name; };
    for(int theme=1;theme<=2;++theme) {
        quiet::ui::preference=theme;quiet::ui::refresh();
        for(auto test:{Test{IDD_PANEL,panelProc,L"panel"},Test{IDD_SETTINGS,settingsProc,L"settings"},
            Test{IDD_HOTKEYS,hotkeyDialogProc,L"hotkeys"},Test{IDD_EQUALIZER,equalizerDialogProc,L"equalizer"}}) {
            HWND dialog=CreateDialogParamW(wc.hInstance,MAKEINTRESOURCEW(test.id),window,test.proc,0);
            if(!dialog) { std::cerr<<"Dialog creation failed\n";ok=false;continue; }
            auto style=GetWindowLongPtrW(dialog,GWL_STYLE);
            if(style&(WS_CAPTION|WS_BORDER|WS_THICKFRAME|WS_DLGFRAME)) { std::cerr<<"Unexpected native frame\n";ok=false; }
            RECT client{};GetClientRect(dialog,&client);
            for(HWND child=GetWindow(dialog,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)) {
                if(!(GetWindowLongPtrW(child,GWL_STYLE)&WS_VISIBLE)) continue;
                RECT r{};GetWindowRect(child,&r);MapWindowPoints(nullptr,dialog,reinterpret_cast<POINT*>(&r),2);
                int id=GetDlgCtrlID(child);
                if(id==IDC_HOTKEY_ANC || id==IDC_EQ_SLIDER_FIRST) {
                    wchar_t name[64]{};GetClassNameW(child,name,64);DWORD_PTR data=0;
                    bool attached=id==IDC_HOTKEY_ANC?GetWindowSubclass(child,quiet::ui::hotkeyPaint,1,&data)!=FALSE:
                        GetWindowSubclass(child,quiet::ui::inputPaint,3,&data)!=FALSE;
                    std::wcout<<L"Paint subclass "<<name<<L": "<<attached<<L"\n";
                    if(!attached) ok=false;
                    if(id==IDC_HOTKEY_ANC) {
                        RECT field{};GetClientRect(child,&field);
                        if(field.right!=r.right-r.left || field.bottom!=r.bottom-r.top) {
                            std::cerr<<"Unexpected non-client edge around hotkey field\n";ok=false;
                        }
                        SendMessageW(child,HKM_SETHOTKEY,MAKEWORD('A',HOTKEYF_CONTROL|HOTKEYF_SHIFT),0);
                        if(SendMessageW(child,HKM_GETHOTKEY,0,0)!=MAKEWORD('A',HOTKEYF_CONTROL|HOTKEYF_SHIFT)) ok=false;
                    }
                }
                if(r.left<0 || r.top<0 || r.right>client.right || r.bottom>client.bottom) {
                    std::cerr<<"Control outside client: "<<GetDlgCtrlID(child)<<" in "<<test.id<<"\n";ok=false;
                }
            }
            if(test.id==IDD_HOTKEYS) {
                HWND field=GetDlgItem(dialog,IDC_HOTKEY_ANC);
                ok=paintIsStable(field)&&ok;
                SendMessageW(field,HKM_SETHOTKEY,0,0);
                ok=paintIsStable(field)&&ok; // placeholder and assigned text
            }
            if(test.id==IDD_EQUALIZER) {
                HWND preset=GetDlgItem(dialog,IDC_EQ_PRESET),slider=GetDlgItem(dialog,IDC_EQ_SLIDER_FIRST);
                ok=hoverStaysIdle(preset)&&hoverStaysIdle(slider)&&ok;
                ok=paintIsStable(preset)&&paintIsStable(slider)&&ok;
                auto initial=SendMessageW(slider,TBM_GETPOS,0,0);
                SendMessageW(slider,WM_KEYDOWN,VK_DOWN,0);SendMessageW(slider,WM_KEYUP,VK_DOWN,0);
                if(SendMessageW(slider,TBM_GETPOS,0,0)!=initial+1 || !eqDirty) {
                    std::cerr<<"Slider keyboard editing broken\n";ok=false;
                }
                auto selection=SendMessageW(preset,CB_GETCURSEL,0,0);
                SendMessageW(preset,CB_SETCURSEL,0,0);
                if(SendMessageW(preset,CB_GETCURSEL,0,0)!=0) ok=false;
                SendMessageW(preset,CB_SETCURSEL,selection,0);
                ok=paintIsStable(slider)&&ok;
            }
            if(argc==2) {
                SetWindowPos(dialog,nullptr,-10000,-10000,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
                ShowWindow(dialog,SW_SHOWNOACTIVATE);UpdateWindow(dialog);
                std::wstring file=std::wstring(argv[1])+L"\\"+test.name+(theme==1?L"-light.png":L"-dark.png");
                if(!savePreview(dialog,file)) { std::cerr<<"Preview failed\n";ok=false; }
            }
            DestroyWindow(dialog);panel=settingsDialog=hotkeyDialog=eqDialog=nullptr;
        }
    }
    DestroyWindow(window);quiet::ui::cleanup();
    std::cout<<(ok?"Native UI frame/bounds, hover, paint stability and input checks passed.\n":"Native UI checks failed.\n");return ok?0:1;
}
