#define NOMINMAX
#include "NativeStreamEpg.h"
#include <iostream>
#include <cstdlib>
static int checks=0;
static void check(bool value,const char* message){++checks;if(!value){std::cerr<<"FAIL "<<message<<"\n";std::exit(1);}}
static std::vector<unsigned char> transport(const std::string& name) {
    std::vector<unsigned char> s={0x42,0xf0,0,0,1,0xc1,0,0,0,1,0xff};
    const size_t n=name.size();
    s.insert(s.end(),{0,1,0xfc,0x80,static_cast<unsigned char>(n+5),0x48,static_cast<unsigned char>(n+3),1,0,static_cast<unsigned char>(n)});
    s.insert(s.end(),name.begin(),name.end());
    size_t length=s.size()+4-3;s[1]|=static_cast<unsigned char>(length>>8);s[2]=static_cast<unsigned char>(length);
    unsigned crc=0xffffffff;
    for(auto b:s){crc^=unsigned(b)<<24;for(int i=0;i<8;++i)crc=crc&0x80000000?(crc<<1)^0x04c11db7:crc<<1;}
    for(int shift:{24,16,8,0})s.push_back(static_cast<unsigned char>(crc>>shift));
    std::vector<unsigned char> result(188*3,0xff);
    for(int i=0;i<3;++i){result[i*188]=0x47;result[i*188+1]=0x1f;result[i*188+2]=0xff;result[i*188+3]=0x10;}
    result[1]=0x40;result[2]=17;result[4]=0;std::copy(s.begin(),s.end(),result.begin()+5);return result;
}
static bool xml(const std::string& value,const std::wstring& name,CNativeProgrammeGuide& g) {
    auto stream=SHCreateMemStream(reinterpret_cast<const BYTE*>(value.data()),UINT(value.size()));
    if(!stream)return false;
    g.stream=L"https://example.test/live/index.m3u8";
    bool result=NativeStreamEpg::ReadGuide(stream,name,g,CNativeProgrammeGuide::Stamp(L"20261026150000 +0300"));stream->Release();return result;
}
int main(){
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    auto ts=transport("Yosso TV Adventure");
    check(NativeStreamEpg::Service(ts)==L"Yosso TV Adventure","extract SDT, not filename");
    auto corrupt=ts;corrupt[30]^=1;check(NativeStreamEpg::Service(corrupt).empty(),"reject SDT CRC mismatch");
    check(NativeStreamEpg::Service(std::vector<unsigned char>(100,0)).empty(),"short malformed TS");
    auto other=transport("Other channel");ts.insert(ts.end(),other.begin(),other.end());
    check(NativeStreamEpg::Service(ts).empty(),"ambiguous services rejected");
    check(NativeStreamEpg::Alias(L"History PAN EURO HD_Virtual_ENC")==L"history","explicit History Europe alias");
    check(NativeStreamEpg::Alias(L"History HD IL").empty(),"preserve regional versions");
    check(NativeStreamEpg::Alias(L"Viasat History").empty(),"preserve different channels");
    const std::string programme="<programme channel=\"test\" start=\"20261026134100 +0300\" stop=\"20261026160200 +0300\"><title>Film &amp; test</title></programme>";
    const std::string guide="<tv><channel id=\"test\"><display-name>YOSSO TV Adventure</display-name></channel>"+programme+"</tv>";
    CNativeProgrammeGuide g;
    check(xml(guide,L"Yosso TV Adventure",g),"exact name match ignores case");
    check(g.rows.size()==1&&g.rows[0].title==L"Film & test","XML escaped title and intervals");
    check(g.rows[0].start==CNativeProgrammeGuide::Stamp(L"20261026154100 +0500"),"local timezone conversion");
    CNativeProgrammeGuide wrong;
    check(!xml(guide,L"Other channel",wrong),"no approximate channel match");
    const auto ambiguous="<tv><channel id=\"test\"><display-name>YOSSO TV Adventure</display-name></channel><channel id=\"other\"><display-name>YOSSO TV Adventure</display-name></channel>"+programme+"</tv>";
    check(!xml(ambiguous,L"Yosso TV Adventure",wrong),"ambiguous XMLTV channels rejected");
    const auto overlapping="<tv><channel id=\"test\"><display-name>YOSSO TV Adventure</display-name></channel>"+programme+"<programme channel=\"test\" start=\"20261026140000 +0300\" stop=\"20261026160000 +0300\"><title>Conflict</title></programme></tv>";
    check(!xml(overlapping,L"Yosso TV Adventure",wrong),"overlapping current programmes rejected");
    check(!xml("<!DOCTYPE tv [<!ENTITY x SYSTEM 'file:///test'>]><tv/>",L"Yosso TV Adventure",wrong),"external entities prohibited");
    check(!xml("<tv><channel id=\"history\"><display-name>History Europe</display-name></channel><programme channel=\"history\" start=\"20261025130000 +0300\" stop=\"20261025140000 +0300\"><title>Old</title></programme></tv>",L"History PAN EURO HD_Virtual_ENC",wrong),"expired guide rejected");
    std::vector<unsigned char> compressed={31,139,8,0,0,0,0,0,2,255,85,141,203,10,194,48,16,69,127,37,100,43,146,105,42,93,77,103,171,191,17,108,180,3,121,145,14,133,250,245,90,90,170,46,6,46,231,112,239,160,204,132,247,209,165,228,131,226,161,215,35,79,146,235,162,9,7,158,74,112,203,57,185,232,233,182,97,52,127,20,205,94,37,44,53,63,171,139,209,171,29,125,167,212,36,174,74,175,45,216,174,89,175,189,52,0,234,4,45,192,42,115,249,113,29,216,195,17,10,75,240,116,125,113,81,15,14,17,205,6,208,28,239,62,89,102,122,3,53,169,198,240,199,0,0,0};
    auto gzip=new NativeStreamEpg::GzipStream(compressed);g.stream=L"https://example.test/live/index.m3u8";
    check(NativeStreamEpg::ReadGuide(gzip,L"History PAN EURO HD_Virtual_ENC",g,CNativeProgrammeGuide::Stamp(L"20261026150000 +0300")),"forward-only gzip XML reader");gzip->Release();
    check(g.channel==L"History"&&g.rows[0].title==L"Gzip film","canonical channel name from XMLTV");
    compressed.resize(compressed.size()-6);gzip=new NativeStreamEpg::GzipStream(compressed);
    check(!NativeStreamEpg::ReadGuide(gzip,L"History",wrong,CNativeProgrammeGuide::Stamp(L"20261026150000 +0300")),"reject truncated gzip");gzip->Release();
    CoUninitialize();std::cout<<checks<<" stream EPG checks passed\n";
}
