// Persistent confirmed stream identities; no credentials or URL tokens on disk.
#pragma once
#include "NativeProgrammeGuide.h"
#include "rapidjson/include/rapidjson/writer.h"
#include "rapidjson/include/rapidjson/stringbuffer.h"
#include <wincrypt.h>
#include <shlobj.h>
#include <map>
#pragma comment(lib,"crypt32.lib")
#pragma comment(lib,"shell32.lib")

namespace StreamIdentity {
inline std::wstring Lower(std::wstring s) {
    if(s.empty())return s;
    std::wstring value(s.size(),L'\0');
    if(LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,s.data(),int(s.size()),value.data(),int(value.size()),nullptr,nullptr,0))return value;
    return s;
}
inline bool Useful(const std::wstring& raw) {
    auto name=Lower(CNativeProgrammeGuide::Clean(raw));
    if(name.empty()||name==L"index"||name.size()>512)return false;
    for(auto c:name)if(c<32)return false;
    for(const auto prefix:{L"service",L"program",L"programme"}) {
        const size_t n=wcslen(prefix);
        if(name.compare(0,n,prefix)==0){
            auto tail=name.substr(n);tail.erase(0,tail.find_first_not_of(L" _-"));
            if(tail.empty()||tail.find_first_not_of(L"0123456789")==tail.npos)return false;
        }
    }
    return true;
}
inline std::string Utf8(const std::wstring& value) {
    const int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),int(value.size()),nullptr,0,nullptr,nullptr);
    if(n<=0)return {};
    std::string s(n,'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),int(value.size()),s.data(),n,nullptr,nullptr);return s;
}
inline std::wstring Wide(const std::string& value) {
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),nullptr,0);
    if(n<=0)return {};
    std::wstring s(n,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),s.data(),n);return s;
}
inline std::string Hash(const std::wstring& text) {
    auto value=Utf8(text);if(value.empty())return {};
    BYTE digest[32];DWORD n=sizeof(digest);
    if(!CryptHashCertificate2(L"SHA256",0,nullptr,reinterpret_cast<const BYTE*>(value.data()),DWORD(value.size()),digest,&n)||n!=32)return {};
    std::string out;for(auto b:digest){out+= "0123456789abcdef"[b>>4];out+="0123456789abcdef"[b&15];}return out;
}
inline std::wstring Transport(const std::wstring& url) {
    if(url.empty()||url.size()>8192||url.find_first_of(L"\r\n")!=url.npos)return {};
    if(_wcsnicmp(url.c_str(),L"http://",7)&&_wcsnicmp(url.c_str(),L"https://",8))return {};
    return url.substr(0,url.find(L'#'));
}
inline std::string ExactKey(const std::wstring& url) {
    auto text=Transport(url);auto hash=Hash(text);return hash.empty()?"":"e:"+hash;
}
inline bool Digits(const std::wstring& s) {return !s.empty()&&s.find_first_not_of(L"0123456789")==s.npos;}
inline std::string ProviderKey(const std::wstring& input) {
    // Only this observed provider format declares the channel in a stable path
    // segment. Unknown formats keep exact URL matching; no global token stripping.
    auto url=Transport(input);if(url.empty())return {};
    auto schemeEnd=url.find(L"://"),begin=schemeEnd+3,slash=url.find(L'/',begin);
    if(slash==url.npos)return {};
    auto scheme=Lower(url.substr(0,schemeEnd));auto host=Lower(url.substr(begin,slash-begin));
    if((scheme==L"https"&&host==L"tvcdnpotok.com:443")||(scheme==L"http"&&host==L"tvcdnpotok.com:80"))host=L"tvcdnpotok.com";
    if(host!=L"tvcdnpotok.com")return {};
    auto queryAt=url.find(L'?',slash);auto path=url.substr(slash+1,queryAt==url.npos?url.npos:queryAt-slash-1);
    std::vector<std::wstring> parts;size_t pos=0;
    for(;;){auto end=path.find(L'/',pos);parts.push_back(path.substr(pos,end==path.npos?path.npos:end-pos));if(end==path.npos)break;pos=end+1;}
    if(parts.size()!=4||parts[3]!=L"index.m3u8"||parts[0].size()<8||parts[0].size()>256
        ||parts[0].find_first_not_of(L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-")!=parts[0].npos
        ||!Digits(parts[1])||parts[1].size()>8||!Digits(parts[2])||parts[2].size()!=10)return {};
    const auto stamp=_wcstoui64(parts[2].c_str(),nullptr,10);
    if(stamp<1577836800ULL||stamp>4102444800ULL)return {};
    // Every query parameter remains significant, including unrecognised source,
    // language and account selectors. Only the two declared path fields rotate.
    auto key=L"tvcdnpotok-v1|"+scheme+L"|"+host+L"|"+parts[1]+L"|"+(queryAt==url.npos?L"":url.substr(queryAt));
    return "p:"+Hash(key);
}
inline std::wstring CacheDirectory(const std::wstring& executable,const std::wstring& localAppData,bool portable) {
    const auto slash=executable.find_last_of(L"\\/");
    if(portable&&slash!=executable.npos)return executable.substr(0,slash+1)+L"EpgCache";
    return localAppData.empty()?L"":localAppData+L"\\MPC-HC\\EpgCache";
}
inline std::wstring Folder() {
    wchar_t path[32768];DWORD n=GetModuleFileNameW(nullptr,path,32768);if(!n||n>=32768)return {};
    const std::wstring executable(path,n);
    const auto dot=executable.find_last_of(L'.');
    const auto ini=dot==executable.npos?executable+L".ini":executable.substr(0,dot)+L".ini";
    const DWORD attributes=GetFileAttributesW(ini.c_str());
    if(attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&FILE_ATTRIBUTE_DIRECTORY)) {
        auto portable=CacheDirectory(executable,{},true);
        if(!portable.empty()&&(CreateDirectoryW(portable.c_str(),nullptr)||GetLastError()==ERROR_ALREADY_EXISTS))return portable;
    }
    // Installed applications never write caches beside the executable in Program Files.
    wchar_t local[MAX_PATH];
    if(FAILED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,SHGFP_TYPE_CURRENT,local)))return {};
    const std::wstring parent=std::wstring(local)+L"\\MPC-HC";
    if(!CreateDirectoryW(parent.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return {};
    auto result=CacheDirectory(executable,local,false);
    if(!CreateDirectoryW(result.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return {};
    return result;
}
struct Binding {
    std::wstring channel,id;std::vector<CNativeProgrammeGuide::Programme> rows;
    ULONGLONG seen=0;bool conflict=false;
};
inline bool Same(const Binding& a,const Binding& b) {
    if(!a.id.empty()&&!b.id.empty())return a.id==b.id;
    return Lower(CNativeProgrammeGuide::Clean(a.channel))==Lower(CNativeProgrammeGuide::Clean(b.channel));
}
class Store {
    std::wstring file;
    class Guard {
        HANDLE handle=nullptr;bool held=false;
    public:
        explicit Guard(const std::wstring& path) {
            auto hash=Wide(Hash(path));handle=CreateMutexW(nullptr,FALSE,(L"Local\\MPC.StreamIdentity."+hash).c_str());
            if(handle){auto r=WaitForSingleObject(handle,2000);held=r==WAIT_OBJECT_0||r==WAIT_ABANDONED;}
        }
        ~Guard(){if(held)ReleaseMutex(handle);if(handle)CloseHandle(handle);}
        explicit operator bool()const{return held;}
    };
    using Entries=std::map<std::string,Binding>;
    Entries Load()const {
        std::ifstream f(file.c_str(),std::ios::binary);if(!f)return {};
        f.seekg(0,std::ios::end);auto n=f.tellg();if(n<=0||n>2097152)return {};f.seekg(0);
        std::string bytes(size_t(n),'\0');if(!f.read(bytes.data(),n))return {};
        rapidjson::Document d;d.Parse(bytes.data(),bytes.size());
        if(d.HasParseError()||!d.IsObject()||!d.HasMember("version")||!d["version"].IsInt()||d["version"].GetInt()!=1
            ||!d.HasMember("bindings")||!d["bindings"].IsArray()||d["bindings"].Size()>512)return {};
        Entries out;const auto now=CNativeProgrammeGuide::Now();
        for(const auto& v:d["bindings"].GetArray()){
            if(!v.IsObject()||!v.HasMember("key")||!v["key"].IsString()||!v.HasMember("channel")||!v["channel"].IsString()
                ||!v.HasMember("seen")||!v["seen"].IsUint64())continue;
            std::string key=v["key"].GetString();if(key.size()!=66||(key.substr(0,2)!="e:"&&key.substr(0,2)!="p:")||key.substr(2).find_first_not_of("0123456789abcdef")!=key.npos)continue;
            Binding b;b.channel=Wide(v["channel"].GetString());b.seen=v["seen"].GetUint64();
            if(!Useful(b.channel)||b.seen>now||now-b.seen>30ULL*24*60*60*10000000)continue;
            if(v.HasMember("id")&&v["id"].IsString())b.id=Wide(v["id"].GetString());if(b.id.size()>256)continue;
            b.conflict=v.HasMember("conflict")&&v["conflict"].IsBool()&&v["conflict"].GetBool();
            if(v.HasMember("rows")&&v["rows"].IsArray()&&v["rows"].Size()<=32){
                for(const auto& r:v["rows"].GetArray()){
                    if(!r.IsObject()||!r.HasMember("start")||!r["start"].IsUint64()||!r.HasMember("stop")||!r["stop"].IsUint64()||!r.HasMember("title")||!r["title"].IsString())continue;
                    CNativeProgrammeGuide::Programme p{r["start"].GetUint64(),r["stop"].GetUint64(),Wide(r["title"].GetString())};
                    if(p.start&&p.stop>p.start&&p.stop>now&&!p.title.empty()&&p.title.size()<=1024)b.rows.push_back(p);
                }
            }
            auto inserted=out.emplace(key,std::move(b));if(!inserted.second)inserted.first->second.conflict=true;
        }
        return out;
    }
    bool Save(Entries entries)const {
        while(entries.size()>512){auto oldest=entries.begin();for(auto i=entries.begin();i!=entries.end();++i)if(i->second.seen<oldest->second.seen)oldest=i;entries.erase(oldest);}
        rapidjson::StringBuffer buffer;rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
        size_t rowBudget=0;
        w.StartObject();w.Key("version");w.Int(1);w.Key("bindings");w.StartArray();
        for(const auto& entry:entries){const auto& b=entry.second;
            w.StartObject();w.Key("key");w.String(entry.first.c_str());w.Key("channel");auto channel=Utf8(b.channel);w.String(channel.c_str());
            w.Key("id");auto id=Utf8(b.id);w.String(id.c_str());w.Key("seen");w.Uint64(b.seen);w.Key("conflict");w.Bool(b.conflict);
            w.Key("rows");w.StartArray();size_t count=0;
            for(const auto& row:b.rows){if(count++>=32)break;auto title=Utf8(row.title);if(rowBudget+2*title.size()+256>262144)break;rowBudget+=2*title.size()+256;w.StartObject();w.Key("start");w.Uint64(row.start);w.Key("stop");w.Uint64(row.stop);w.Key("title");w.String(title.c_str());w.EndObject();}
            w.EndArray();w.EndObject();
        }
        w.EndArray();w.EndObject();if(buffer.GetSize()>2097152)return false;
        auto temp=file+L".tmp."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetCurrentThreadId());
        std::ofstream f(temp.c_str(),std::ios::binary);if(!f.write(buffer.GetString(),buffer.GetSize())){f.close();DeleteFileW(temp.c_str());return false;}
        f.flush();bool complete=bool(f);f.close();complete=complete&&!f.fail();
        if(complete&&MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return true;
        DeleteFileW(temp.c_str());return false;
    }
public:
    explicit Store(std::wstring folder=Folder()):file(folder.empty()?L"":std::move(folder)+L"\\stream-identities.json"){}
    Binding Lookup(const std::wstring& url)const {
        if(file.empty())return {};Guard guard(file);if(!guard)return {};auto entries=Load();
        for(const auto& key:{ExactKey(url),ProviderKey(url)}){auto it=entries.find(key);if(it!=entries.end()){if(it->second.conflict)return {};return it->second;}}
        return {};
    }
    bool Remember(const std::wstring& url,Binding binding)const {
        if(file.empty()||!Useful(binding.channel)||ExactKey(url).empty())return false;
        Guard guard(file);if(!guard)return false;auto entries=Load();binding.seen=CNativeProgrammeGuide::Now();
        for(const auto& key:{ExactKey(url),ProviderKey(url)}){
            if(key.empty())continue;auto it=entries.find(key);
            if(it!=entries.end()&&(it->second.conflict||!Same(it->second,binding))){it->second.conflict=true;it->second.seen=binding.seen;continue;}
            entries[key]=binding;
        }
        return Save(std::move(entries));
    }
};
}
