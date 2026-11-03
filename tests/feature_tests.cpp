#include "../src/features.hpp"
#include <iostream>
using namespace sony;
void check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
template<class F> void rejected(F fn,const char* message) {bool failed=false;try{fn();}catch(const FeatureError&){failed=true;}check(failed,message);}
int main() {
    try {
        // Real WF-1000XM5 6.1.0 replies (no addresses/identifiers).
        check(parseFirmware({5,2,5,'6','.','1','.','0'})=="6.1.0","firmware fixture");
        rejected([]{parseFirmware({5,2,10,'6'});},"truncated firmware");
        auto support=parseSupport({7,0,3,0x29,0xff,0xfc,0x0d,0x50,0x11});
        check(support.count(0x29) && support.count(0xfc) && !support.count(0x2a),"capability pairs");
        rejected([]{parseSupport({7,0,3,0x29});},"truncated support");
        FeatureState f;
        check(parseBattery({0x23,9,0,0,69,0,100,100},f),"threshold battery packet");
        check(f.left.percent==0 && f.right.percent==69,"thresholds are not battery levels");
        check(parseConnections({0x13,1,0,1},f) && f.leftConnected==false && f.rightConnected==true,"disconnected left earbud");
        check(parseBattery({0x23,10,100,0,30},f) && f.cradle.percent==100,"case value not threshold");
        check(parseBattery({0x25,1,255,2,70,1},f) && !f.left.percent && f.right.charging==1,"unknown is not zero");
        rejected([&]{parseBattery({0x23,9,10,0,70,0},f);},"short threshold reply");
        rejected([&]{parseBattery({0x25,1,200,0,70,0},f);},"bad battery level");
        check(!parseConnections({0x13,1,255,1},f),"unknown connection not connected");
        check(parseSpeak({0xf7,0x0c,1,1})==false,"Speak off uses one");
        check(parseSpeak({0xf9,0x0c,0,1})==true,"Speak notification on uses zero");
        check(!parseSpeak({0xf7,0x0c,0}),"short Speak reply");
        Bytes gs{0xd1,0xd1,0,1,16};
        std::string name="SIDETONE_SETTING",summary="SIDETONE_SETTING_SUMMARY";
        gs.insert(gs.end(),name.begin(),name.end());gs.push_back(uint8_t(summary.size()));gs.insert(gs.end(),summary.begin(),summary.end());
        check(isSidetoneCapability(gs,0xd1),"identify sidetone by advertised name");
        gs[5]='X';check(!isSidetoneCapability(gs,0xd1),"never assume GS1 is sidetone");
        check(parseSidetone({0xd7,0xd1,0,0},0xd1)==true,"sidetone on");
        check(!parseSidetone({0xd7,0xd2,0,0},0xd1),"other GS cannot overwrite sidetone");
        Bytes cap{0x51,0,6,21,12,0,0,0x10,0,0x11,0,0x12,0,0x13,0,0x14,0,0x15,0,0x16,0,0x17,0,0xa0,0,0xa1,0,0xa2,0};
        auto presets=parseEqCapabilities(cap);
        check(presets.presets.size()==12 && presets.presets.back()==0xa2,"advertised preset fixture");
        cap.pop_back();rejected([&]{parseEqCapabilities(cap);},"truncated preset list");
        rejected([]{parseEqCapabilities({0x51,0,10,21,1,0,0});},"unsupported band layout");
        rejected([]{parseEqCapabilities({0x51,0,6,21,2,0,0,0,0});},"duplicate presets");
        auto eq=parseEq({0x57,0,0,6,10,10,10,10,10,10});
        check(eq && eq->bands[0]==10 && eq->preset==0,"actual flat EQ fixture");
        auto custom=parseEq({0x59,0,0xa0,6,0,20,9,11,10,10});
        check(custom && eqBandsCommand(*custom)==Bytes({0x58,0,0xa0,6,0,20,9,11,10,10}),"custom command preserves six values");
        check(!parseEq({0x57,0,0,6,21,10,10,10,10,10}),"reject out-of-range band");
        check(!parseEq({0x57,0,0,6,10}),"reject incomplete EQ");
        std::cout<<"All feature parser tests passed.\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
