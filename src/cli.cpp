#include "bluetooth.hpp"
#include <iostream>
#include <iomanip>
void printFeatures(const sony::FeatureState& f) {
    auto percent=[](const sony::Battery& b) { return b.percent ? std::to_string(*b.percent)+"%" : "unknown"; };
    auto boolean=[](std::optional<bool> v) { return !v ? "unknown" : *v ? "on" : "off"; };
    std::cout << "Firmware: " << f.firmware << "\nLeft: " << percent(f.left) << " (connected=" << boolean(f.leftConnected)
        << ")\nRight: " << percent(f.right) << " (connected=" << boolean(f.rightConnected)
        << ")\nCase (headset-reported): " << percent(f.cradle)
        << "\nSpeak-to-Chat: " << boolean(f.speak) << "\nSidetone: " << boolean(f.sidetone)
        << "\nEQ available: " << boolean(f.eqAvailable) << '\n';
    if(f.equalizer) { std::cout << "EQ preset: " << int(f.equalizer->preset) << " / values:";
        for(auto b:f.equalizer->bands) std::cout << ' ' << int(b)-10;
        std::cout << '\n';
    }
    if(!f.warning.empty()) std::cout << "Feature warning: " << f.warning << '\n';
}
void verifyFeatures(sony::Session& session) {
    auto toggle=[&](const char* name,std::optional<bool> original,auto set) {
        if(!original) throw std::runtime_error(std::string(name)+" state unavailable");
        std::exception_ptr failure;
        try { set(!*original); std::cout << name << ": opposite state confirmed\n"; }
        catch(...) { failure=std::current_exception(); }
        try { set(*original); std::cout << name << ": original state restored\n"; }
        catch(const std::exception& e) { throw std::runtime_error(std::string("RESTORE FAILED for ")+name+": "+e.what()); }
        if(failure) std::rethrow_exception(failure);
    };
    toggle("Speak-to-Chat",session.features.speak,[&](bool v){session.setSpeak(v);});
    toggle("Sidetone",session.features.sidetone,[&](bool v){session.setSidetone(v);});
    const auto original=session.readEq();
    std::optional<sony::EqState> custom;
    std::exception_ptr failure;
    try {
        session.setEqPreset(0x10); std::cout << "Bright preset confirmed\n";
        session.setEqPreset(0xa0); custom=session.readEq();
        auto target=*custom; target.bands[0]=uint8_t(target.bands[0]>0 ? target.bands[0]-1 : 1);
        session.setEqBands(target); std::cout << "Custom Clear Bass change confirmed\n";
    } catch(...) { failure=std::current_exception(); }
    std::string restoreError;
    if(custom) try { session.setEqBands(*custom); std::cout << "Custom EQ slot restored\n"; }
        catch(const std::exception& e) {restoreError=e.what();}
    try { session.setEqPreset(original.preset); std::cout << "Original EQ preset restored\n"; }
        catch(const std::exception& e) {restoreError+=std::string(" ")+e.what();}
    if(!restoreError.empty()) throw std::runtime_error("EQ RESTORE FAILED: "+restoreError);
    if(failure) std::rethrow_exception(failure);
}
int main(int argc, char** argv) {
    try {
        sony::Mode mode = sony::Mode::Unknown;
        if (argc > 2) throw std::runtime_error("Usage: S-XMTray-cli [status|anc|ambient|off]");
        if (argc == 2) {
            std::string arg = argv[1];
            if (arg == "anc") mode = sony::Mode::Anc;
            else if (arg == "ambient") mode = sony::Mode::Ambient;
            else if (arg == "off") mode = sony::Mode::Off;
            else if (arg != "status" && arg != "inspect" && arg != "verify-features") throw std::runtime_error("Usage: S-XMTray-cli [status|inspect|verify-features|anc|ambient|off]");
        }
        sony::Winsock winsock;
        sony::Session session;
        session.connectDevice();
        std::cout << "Connected: WF-1000XM5\nMode: " << sony::modeName(session.state.mode)
                  << "\nAmbient level: " << int(session.state.level) << "\n";
        printFeatures(session.features);
        if (argc==2 && std::string(argv[1])=="verify-features") verifyFeatures(session);
        if (argc == 2 && std::string(argv[1]) == "inspect") {
            for (const auto& entry : session.inspectFeatures()) {
                std::cout << entry.first << ": ";
                for (auto b : entry.second) std::cout << std::hex << std::setw(2) << std::setfill('0') << int(b) << ' ';
                std::cout << std::dec << '\n';
            }
        }
        if (mode != sony::Mode::Unknown) {
            session.setMode(mode);
            std::cout << "Confirmed mode: " << sony::modeName(session.state.mode) << "\n";
        }
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << "\n"; return 1; }
}
