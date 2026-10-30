#define NOMINMAX
#include "NativeStreamIdentity.h"
#include <iostream>
#include <cstdlib>
#include <sstream>
static int checks=0;
static void check(bool value,const char* message){++checks;if(!value){std::cerr<<"FAIL "<<message<<"\n";std::exit(1);}}
int main(){
    using namespace StreamIdentity;
    const std::wstring a=L"https://tvcdnpotok.com/abcdefgh_X/570/1793037301/index.m3u8";
    const std::wstring b=L"https://tvcdnpotok.com/newtoken-abc/570/1793137301/index.m3u8";
    check(!ProviderKey(a).empty()&&ProviderKey(a)==ProviderKey(b),"rotating signed path preserves provider channel");
    check(ExactKey(a)!=ExactKey(b),"exact URLs remain different");
    check(ProviderKey(a)!=ProviderKey(L"https://tvcdnpotok.com/abcdefgh_X/111/1793037301/index.m3u8"),"other channel excluded");
    check(ProviderKey(a)!=ProviderKey(a+L"?source=other"),"unknown query is significant");
    check(ProviderKey(a+L"?lang=ru")!=ProviderKey(a+L"?lang=en"),"language selector preserved");
    check(ProviderKey(a+L"?account=1")!=ProviderKey(a+L"?account=2"),"account selector preserved");
    check(ProviderKey(L"https://other.example/abcdefgh_X/570/1793037301/index.m3u8").empty(),"unknown provider exact-only");
    check(ProviderKey(L"https://tvcdnpotok.com.evil.test/abcdefgh_X/570/1793037301/index.m3u8").empty(),"host suffix spoof rejected");
    check(ProviderKey(L"https://user@tvcdnpotok.com/abcdefgh_X/570/1793037301/index.m3u8").empty(),"userinfo does not generalise");
    check(ProviderKey(L"https://tvcdnpotok.com:8443/abcdefgh_X/570/1793037301/index.m3u8").empty(),"non-default port exact-only");
    check(ProviderKey(L"https://tvcdnpotok.com:443/abcdefgh_X/570/1793037301/index.m3u8")==ProviderKey(a),"default port equivalent");
    check(ProviderKey(L"https://tvcdnpotok.com/570/index.m3u8").empty(),"unconfirmed short URL schema exact-only");
    check(ProviderKey(L"https://tvcdnpotok.com/short/570/1793037301/index.m3u8").empty(),"malformed token exact-only");
    check(ProviderKey(L"https://tvcdnpotok.com/abcdefgh_X/570/1793037301/extra/index.m3u8").empty(),"extra path exact-only");
    check(ProviderKey(L"https://tvcdnpotok.com/abcdefgh_X/570/not-a-date/index.m3u8").empty(),"invalid expiry exact-only");
    check(ProviderKey(a)!=ProviderKey(L"http://tvcdnpotok.com/abcdefgh_X/570/1793037301/index.m3u8"),"scheme retained");
    check(ExactKey(a)==ExactKey(a+L"#fragment"),"HTTP fragment not transmitted");
    check(ExactKey(L"C:\\video\\index.m3u8").empty(),"local path excluded");
    check(ExactKey(a+L"\r\nHost: other").empty(),"control bytes rejected");
    check(!Useful(L"Service01")&&!Useful(L"Program 1")&&!Useful(L"index"),"generic names rejected");
    check(Useful(L"TV Channel 1")&&Useful(L"ID Xtra EU"),"meaningful labels accepted");
    check(Lower(L"ПЕРВЫЙ") == L"первый","Unicode case folding");
    check(Hash(L"abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA256 implementation");
    wchar_t temp[MAX_PATH];check(GetTempPathW(MAX_PATH,temp)!=0,"temporary directory available");
    auto folder=std::wstring(temp)+L"MPCStreamIdentityTests."+std::to_wstring(GetCurrentProcessId());
    check(CreateDirectoryW(folder.c_str(),nullptr)!=0,"isolated test directory created");
    Store store(folder);Binding value;value.channel=L"Confirmed channel";value.id=L"confirmed-id";
    auto now=CNativeProgrammeGuide::Now();value.rows.push_back({now-600000000,now+36000000000,L"Current programme"});
    check(store.Remember(a,value),"confirmed identity saved");
    check(store.Lookup(b).id==value.id,"new token restores learned XMLTV ID");
    check(Store(folder).Lookup(b).rows.size()==1,"restart retains dated fallback context");
    CNativeProgrammeGuide g;auto restored=store.Lookup(b);g.stream=b;g.channel=restored.channel;g.rows=restored.rows;
    check(g.Current(b,now)&&g.Current(b,now)->title==L"Current programme","cached rows stay source-bound");
    check(!g.Current(a,now),"old URL cannot masquerade as current source");
    check(store.Lookup(a+L"?source=other").channel.empty(),"unseen selector cannot reuse channel");
    std::ifstream f((folder+L"\\stream-identities.json").c_str(),std::ios::binary);std::ostringstream content;content<<f.rdbuf();f.close();
    check(content.str().find("abcdefgh_X")==std::string::npos&&content.str().find("tvcdnpotok.com")==std::string::npos,"tokens and raw URLs absent on disk");
    auto alias=value;alias.channel=L"Updated display name";check(store.Remember(b,alias),"same XMLTV ID accepts display-name alias");
    auto wrong=value;wrong.channel=L"Other channel";wrong.id=L"different-id";
    check(store.Remember(b,wrong),"conflict recorded");
    check(store.Lookup(b).channel.empty(),"conflicting exact key disabled");
    check(store.Lookup(L"https://tvcdnpotok.com/thirdtoken/570/1793237301/index.m3u8").channel.empty(),"conflicting provider key disabled");
    check(store.Lookup(a).id==value.id,"independently confirmed old exact URL retained");
    check(!store.Remember(a,Binding{L"Service01",L"",{},0,false}),"placeholder never learned");
    const auto path=folder+L"\\stream-identities.json";
    rapidjson::Document doc;doc.Parse(content.str().c_str());
    for(auto& item:doc["bindings"].GetArray())item["seen"].SetUint64(now-31ULL*24*60*60*10000000);
    rapidjson::StringBuffer buffer;rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);doc.Accept(writer);
    std::ofstream old(path.c_str(),std::ios::binary);old.write(buffer.GetString(),buffer.GetSize());old.close();
    check(store.Lookup(a).channel.empty(),"expired identity not reused");
    std::ofstream bad(path.c_str());bad<<"{invalid";bad.close();check(store.Lookup(a).channel.empty(),"corrupt store safely ignored");
    check(DeleteFileW(path.c_str())!=0&&RemoveDirectoryW(folder.c_str())!=0,"isolated test files removed");
    std::cout<<checks<<" identity checks passed\n";
}
