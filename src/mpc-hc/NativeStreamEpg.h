// Optional bare-HLS programme resolver. GPLv3+.
#pragma once
#include "NativeProgrammeGuide.h"
#include "NativeStreamIdentity.h"
#include <set>
#include <map>
#include <mutex>
#include <memory>
#include <thread>
#include <atomic>
#include <cwctype>
#include <xmllite.h>
#include <shlwapi.h>
#pragma comment(lib, "xmllite.lib")
#pragma comment(lib, "shlwapi.lib")

namespace NativeStreamEpg {
inline std::wstring Name(std::wstring s) {
    s=CNativeProgrammeGuide::Clean(s);
    return StreamIdentity::Lower(s);
}
inline std::wstring Utf8(const char* p, int n) {
    const int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,p,n,nullptr,0);
    if(size<=0)return {};
    std::wstring s(size,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,p,n,s.data(),size);return s;
}
inline std::wstring Dvb(const unsigned char* p,size_t n) {
    if(!n)return {};
    UINT cp=CP_UTF8;
    if(p[0]==0x01){++p;--n;cp=28595;}
    else if(p[0]==0x15){++p;--n;}
    else {for(size_t i=0;i<n;++i)if(p[i]<32||p[i]>=127)return {};}
    int k=MultiByteToWideChar(cp,0,reinterpret_cast<const char*>(p),int(n),nullptr,0);
    if(k<=0)return {};
    std::wstring s(k,L'\0');MultiByteToWideChar(cp,0,reinterpret_cast<const char*>(p),int(n),s.data(),k);return s;
}
inline bool Crc(const std::vector<unsigned char>& s) {
    unsigned crc=0xffffffff;
    for(auto b:s){crc^=unsigned(b)<<24;for(int i=0;i<8;++i)crc=crc&0x80000000?(crc<<1)^0x04c11db7:crc<<1;}
    return crc==0;
}
inline void Sdt(const std::vector<unsigned char>& s,std::set<std::wstring>& names) {
    if(s.size()<15||s[0]!=0x42||!(s[5]&1)||!Crc(s))return;
    const size_t end=s.size()-4;
    for(size_t i=11;i+5<=end;){
        size_t stop=i+5+((s[i+3]&15)*256+s[i+4]);if(stop>end)return;
        for(size_t j=i+5;j+2<=stop;){
            size_t next=j+2+s[j+1];if(next>stop)return;
            if(s[j]==0x48&&s[j+1]>=3){
                size_t k=j+4+s[j+3];
                if(k<next&&k+1+s[k]<=next){auto name=Dvb(s.data()+k+1,s[k]);if(!name.empty())names.insert(name);}
            }
            j=next;
        }
        i=stop;
    }
}
inline std::wstring Service(const std::vector<unsigned char>& bytes) {
    std::set<std::wstring> names;std::vector<unsigned char> section;int continuity=-1;
    size_t offset=0;
    while(offset+376<bytes.size()&&!(bytes[offset]==0x47&&bytes[offset+188]==0x47&&bytes[offset+376]==0x47))++offset;
    for(size_t i=offset;i+188<=bytes.size();i+=188){
        auto p=bytes.data()+i;
        if(p[0]!=0x47||(p[1]&0x80)||((p[1]&31)*256+p[2])!=17||!(p[3]&16))continue;
        int cc=p[3]&15;if(continuity>=0&&cc!=((continuity+1)&15))section.clear();continuity=cc;
        size_t j=4;if(p[3]&32)j+=1+p[4];if(j>=188)continue;
        if(p[1]&64){j+=1+p[j];section.clear();if(j>=188)continue;}
        section.insert(section.end(),p+j,p+188);
        while(section.size()>=3){
            if(section[0]==255){section.clear();break;}
            size_t size=3+((section[1]&15)*256+section[2]);
            if(size>4096||size<3){section.clear();break;}
            if(section.size()<size)break;
            std::vector<unsigned char> one(section.begin(),section.begin()+size);Sdt(one,names);
            section.erase(section.begin(),section.begin()+size);
        }
    }
    return names.size()==1?*names.begin():L"";
}
inline std::wstring Attribute(IXmlReader* r,const wchar_t* key) {
    if(r->MoveToAttributeByName(key,nullptr)!=S_OK)return {};
    const wchar_t* value=nullptr;UINT n=0;r->GetValue(&value,&n);std::wstring s(value?value:L"",n);r->MoveToElement();return s;
}
inline bool ReadGuide(IStream* stream,const std::wstring& service,CNativeProgrammeGuide& guide,ULONGLONG now=CNativeProgrammeGuide::Now(),const std::wstring& expectedId=L"",const CNativeProgrammeGuide* context=nullptr) {
    IXmlReader* raw=nullptr;if(FAILED(CreateXmlReader(__uuidof(IXmlReader),reinterpret_cast<void**>(&raw),nullptr)))return false;
    std::unique_ptr<IXmlReader,void(*)(IXmlReader*)> reader(raw,[](IXmlReader* p){p->Release();});
    // XMLTV declares an external DTD; ignore it without resolving or loading it.
    raw->SetProperty(XmlReaderProperty_DtdProcessing,DtdProcessing_Parse);
    raw->SetProperty(XmlReaderProperty_XmlResolver,0);
    raw->SetProperty(XmlReaderProperty_MaxEntityExpansion,4096);
    if(FAILED(raw->SetInput(stream)))return false;
    std::set<std::wstring> matches;std::map<std::wstring,std::wstring> labels;std::wstring channelId,display,programmeId,field;unsigned channelDepth=0,programmeDepth=0;
    CNativeProgrammeGuide::Programme programme;std::map<std::wstring,std::vector<CNativeProgrammeGuide::Programme>> candidates;
    const auto wanted=Name(service);XmlNodeType type;HRESULT hr;
    std::map<std::wstring,std::set<ULONGLONG>> fingerprints;
    const bool contextCurrent=context&&context->Current(context->stream,now);
    auto sameProgramme=[&](const CNativeProgrammeGuide::Programme& p){
        if(!contextCurrent)return false;
        for(const auto& r:context->rows){
            if(r.stop<=now||r.start>now+2ULL*24*60*60*10000000)continue;
            if(p.start/600000000==r.start/600000000&&p.stop/600000000==r.stop/600000000&&Name(p.title)==Name(r.title))return true;
        }
        return false;
    };
    // XMLTV channels precede programmes; only retain rows for matching channels.
    while((hr=raw->Read(&type))==S_OK){
        const wchar_t* ptr=nullptr;UINT n=0;raw->GetLocalName(&ptr,&n);std::wstring tag(ptr?ptr:L"",n);UINT depth=0;raw->GetDepth(&depth);
        if(type==XmlNodeType_Element){
            if(tag==L"channel"){channelId=Attribute(raw,L"id");channelDepth=depth;display.clear();}
            else if(tag==L"programme"){
                programmeId=Attribute(raw,L"channel");programmeDepth=depth;
                programme={CNativeProgrammeGuide::Stamp(Attribute(raw,L"start")),CNativeProgrammeGuide::Stamp(Attribute(raw,L"stop")),{}};
            }
            if(tag==L"display-name"&&!channelId.empty()){field=tag;display.clear();}
            else if(tag==L"title"&&!programmeId.empty()&&programme.title.empty())field=tag;
        }else if(type==XmlNodeType_Text||type==XmlNodeType_CDATA){
            const wchar_t* value=nullptr;UINT count=0;raw->GetValue(&value,&count);
            if(count>4096)return false;
            if(field==L"display-name")display.append(value,count);
            else if(field==L"title")programme.title.append(value,count);
        }else if(type==XmlNodeType_EndElement){
            // XmlLite reports closing elements one level deeper than their opening node.
            if(tag==L"display-name"){
                if(expectedId.empty()?Name(display)==wanted:channelId==expectedId)matches.insert(channelId);
                if(!labels.count(channelId))labels[channelId]=CNativeProgrammeGuide::Clean(display);
                field.clear();
            }else if(tag==L"title")field.clear();
            else if(tag==L"channel"&&depth==channelDepth+1){channelId.clear();}
            else if(tag==L"programme"&&depth==programmeDepth+1){
                if(programme.start&&programme.stop>programme.start&&!programme.title.empty()){
                    programme.title=CNativeProgrammeGuide::Clean(programme.title);
                    if(expectedId.empty()&&sameProgramme(programme))fingerprints[programmeId].insert(programme.start);
                    if(!matches.count(programmeId)&&!fingerprints.count(programmeId)){programmeId.clear();field.clear();continue;}
                    auto& rows=candidates[programmeId];if(rows.size()>=4096)return false;rows.push_back(programme);
                }
                programmeId.clear();field.clear();
            }
        }
    }
    if(hr!=S_FALSE)return false;
    if(matches.empty()&&expectedId.empty())for(const auto& f:fingerprints)if(f.second.size()>=2)matches.insert(f.first);
    if(matches.size()!=1)return false;
    guide.xmltvId=*matches.begin();
    guide.channel=labels[*matches.begin()];guide.rows=std::move(candidates[*matches.begin()]);
    std::sort(guide.rows.begin(),guide.rows.end(),[](const auto& a,const auto& b){return a.start<b.start;});
    guide.rows.erase(std::unique(guide.rows.begin(),guide.rows.end(),[](const auto& a,const auto& b){return a.start==b.start&&a.stop==b.stop&&a.title==b.title;}),guide.rows.end());
    return guide.Current(guide.stream,now)!=nullptr;
}
}

#ifndef MPC_EPG_PARSER_ONLY
#include <wininet.h>
#include "zlib/zlib.h"
#pragma comment(lib, "wininet.lib")

namespace NativeStreamEpg {
class InternetHandle {
public:HINTERNET h=nullptr;explicit InternetHandle(HINTERNET v):h(v){}~InternetHandle(){if(h)InternetCloseHandle(h);}operator HINTERNET()const{return h;}
};
inline std::vector<unsigned char> Fetch(std::wstring url,size_t limit,bool sample,const std::shared_ptr<std::atomic_bool>& cancel,std::wstring* finalUrl=nullptr) {
    if(url.find_first_of(L"\r\n")!=url.npos||(url.compare(0,7,L"http://")&&url.compare(0,8,L"https://")))return {};
    InternetHandle session(InternetOpenW(L"Mozilla/5.0 (compatible; MPC-HC EPG)",INTERNET_OPEN_TYPE_PRECONFIG,nullptr,nullptr,0));
    if(!session.h)return {};
    DWORD timeout=10000;
    for(DWORD option:{INTERNET_OPTION_CONNECT_TIMEOUT,INTERNET_OPTION_SEND_TIMEOUT,INTERNET_OPTION_RECEIVE_TIMEOUT})InternetSetOptionW(session,option,&timeout,sizeof(timeout));
    if(cancel->load())return {};
    InternetHandle request(InternetOpenUrlW(session,url.c_str(),nullptr,0,INTERNET_FLAG_RELOAD|INTERNET_FLAG_NO_CACHE_WRITE|INTERNET_FLAG_NO_COOKIES|INTERNET_FLAG_NO_AUTH,0));
    if(!request.h)return {};
    DWORD status=0,size=sizeof(status);if(!HttpQueryInfoW(request,HTTP_QUERY_STATUS_CODE|HTTP_QUERY_FLAG_NUMBER,&status,&size,nullptr)||(status!=200&&status!=206))return {};
    if(finalUrl){DWORD length=65536;std::wstring actual(length/sizeof(wchar_t),L'\0');if(InternetQueryOptionW(request,INTERNET_OPTION_URL,actual.data(),&length))*finalUrl=actual.c_str();}
    std::vector<unsigned char> bytes;const auto started=GetTickCount64();
    for(;;){
        if(cancel->load()||GetTickCount64()-started>180000)return {};
        unsigned char block[32768];DWORD count=0;DWORD want=DWORD((std::min)(sizeof(block),limit+1-bytes.size()));
        if(!want)return sample?bytes:std::vector<unsigned char>{};
        if(!InternetReadFile(request,block,want,&count))return {};
        if(!count)break;bytes.insert(bytes.end(),block,block+count);
        if(bytes.size()>limit)return {};
        if(sample&&bytes.size()>=limit)break;
    }
    return bytes;
}
inline std::wstring Combine(const std::wstring& base,const std::wstring& relative) {
    DWORD n=32768;std::wstring value(n,L'\0');
    if(FAILED(UrlCombineW(base.c_str(),relative.c_str(),value.data(),&n,URL_DONT_ESCAPE_EXTRA_INFO)))return {};
    value.resize(n);return value;
}
inline std::wstring Probe(const std::wstring& input,const std::shared_ptr<std::atomic_bool>& cancel) {
    std::wstring url=input;
    for(int level=0;level<3;++level){
        std::wstring base=url;auto bytes=Fetch(url,512*1024,false,cancel,&base);if(bytes.empty())return {};
        auto text=Utf8(reinterpret_cast<const char*>(bytes.data()),int(bytes.size()));
        if(text.size()&&text[0]==0xfeff)text.erase(0,1);
        if(text.compare(0,7,L"#EXTM3U"))return {};
        bool master=false;std::wstring next;size_t pos=0;
        while(pos<text.size()){
            auto end=text.find(L'\n',pos);auto line=CNativeProgrammeGuide::Clean(text.substr(pos,end==text.npos?text.npos:end-pos));pos=end==text.npos?text.size():end+1;
            if(line.find(L"#EXT-X-STREAM-INF:")==0)master=true;
            if(!line.empty()&&line[0]!=L'#'){next=line;break;}
        }
        if(next.empty())return {};next=Combine(base,next);if(next.empty())return {};
        if(master){url=next;continue;}
        return Service(Fetch(next,512*1024,true,cancel));
    }
    return {};
}
// Forward-only gzip IStream: XmlLite parses the full guide without a large DOM
// or a second uncompressed 200+ MB buffer/file.
class GzipStream final:public IStream {
    LONG refs=1;z_stream z{};bool valid=false;size_t total=0;
public:
    explicit GzipStream(const std::vector<unsigned char>& bytes){z.next_in=const_cast<Bytef*>(bytes.data());z.avail_in=uInt(bytes.size());valid=inflateInit2(&z,15+32)==Z_OK;}
    ~GzipStream(){if(valid)inflateEnd(&z);}
    STDMETHODIMP QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(id==IID_IUnknown||id==IID_ISequentialStream||id==IID_IStream){*out=static_cast<IStream*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    STDMETHODIMP_(ULONG) AddRef()override{return InterlockedIncrement(&refs);}
    STDMETHODIMP_(ULONG) Release()override{auto n=InterlockedDecrement(&refs);if(!n)delete this;return n;}
    STDMETHODIMP Read(void* out,ULONG count,ULONG* got)override{
        if(got)*got=0;if(!valid)return STG_E_READFAULT;z.next_out=static_cast<Bytef*>(out);z.avail_out=count;
        const int status=inflate(&z,Z_NO_FLUSH);ULONG n=count-z.avail_out;total+=n;if(total>384*1024*1024)return STG_E_READFAULT;
        if(status!=Z_STREAM_END&&z.avail_in==0&&z.avail_out>0)return STG_E_READFAULT;
        if(got)*got=n;if(status!=Z_OK&&status!=Z_STREAM_END&&status!=Z_BUF_ERROR)return STG_E_READFAULT;
        return n==count?S_OK:S_FALSE;
    }
    STDMETHODIMP Write(const void*,ULONG,ULONG*)override{return E_NOTIMPL;}
    STDMETHODIMP Seek(LARGE_INTEGER,DWORD,ULARGE_INTEGER*)override{return E_NOTIMPL;}
    STDMETHODIMP SetSize(ULARGE_INTEGER)override{return E_NOTIMPL;}
    STDMETHODIMP CopyTo(IStream*,ULARGE_INTEGER,ULARGE_INTEGER*,ULARGE_INTEGER*)override{return E_NOTIMPL;}
    STDMETHODIMP Commit(DWORD)override{return E_NOTIMPL;}
    STDMETHODIMP Revert()override{return E_NOTIMPL;}
    STDMETHODIMP LockRegion(ULARGE_INTEGER,ULARGE_INTEGER,DWORD)override{return E_NOTIMPL;}
    STDMETHODIMP UnlockRegion(ULARGE_INTEGER,ULARGE_INTEGER,DWORD)override{return E_NOTIMPL;}
    STDMETHODIMP Stat(STATSTG* s,DWORD)override{if(!s)return E_POINTER;*s={};s->type=STGTY_STREAM;return S_OK;}
    STDMETHODIMP Clone(IStream**)override{return E_NOTIMPL;}
};
inline std::vector<unsigned char> GuideBytes(const std::shared_ptr<std::atomic_bool>& cancel) {
    if(cancel->load())return {};
    auto folder=StreamIdentity::Folder();if(folder.empty())return {};
    auto file=folder+L"\\iptvx-lite.xml.gz";WIN32_FILE_ATTRIBUTE_DATA info{};
    if(GetFileAttributesExW(file.c_str(),GetFileExInfoStandard,&info)&&!info.nFileSizeHigh&&info.nFileSizeLow<=40*1024*1024){
        ULONGLONG written=(ULONGLONG(info.ftLastWriteTime.dwHighDateTime)<<32)|info.ftLastWriteTime.dwLowDateTime;
        auto now=CNativeProgrammeGuide::Now();if(now>=written&&now-written<12ULL*60*60*10000000){
            std::ifstream f(file.c_str(),std::ios::binary);std::vector<unsigned char> b(info.nFileSizeLow);if(f.read(reinterpret_cast<char*>(b.data()),b.size()))return b;
        }
    }
    auto b=Fetch(L"https://iptvx.one/EPG_LITE",40*1024*1024,false,cancel);
    if(b.size()<2||b[0]!=0x1f||b[1]!=0x8b)return {};
    // Failed/partial HTTP responses never replace the last successful download.
    auto temporary=file+L".tmp."+std::to_wstring(GetCurrentThreadId());
    std::ofstream f(temporary.c_str(),std::ios::binary);if(f.write(reinterpret_cast<const char*>(b.data()),b.size())){f.close();MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING);}else{f.close();DeleteFileW(temporary.c_str());}return b;
}
class Resolver {
    struct Result{std::mutex mutex;bool done=false;CNativeProgrammeGuide guide;std::shared_ptr<std::atomic_bool> cancel=std::make_shared<std::atomic_bool>(false);};
    std::shared_ptr<Result> result;std::wstring source,remembered;ULONGLONG requested=0;bool delivered=false;
public:
    ~Resolver(){Reset();}
    void Reset(){if(result)result->cancel->store(true);result.reset();source.clear();remembered.clear();requested=0;delivered=false;}
    void Remember(const CNativeProgrammeGuide& imported){
        if(!StreamIdentity::Useful(imported.channel)||StreamIdentity::Transport(imported.pageUrl).empty()||StreamIdentity::ExactKey(imported.stream).empty())return;
        const auto signature=imported.stream+L"\n"+imported.channel;
        if(remembered==signature)return;remembered=signature;
        auto context=imported;
        try{std::thread([context]{
            try{
                StreamIdentity::Store store;StreamIdentity::Binding binding;
                binding.channel=context.channel;binding.id=context.xmltvId;
                for(const auto& row:context.Remaining(CNativeProgrammeGuide::Now())){if(binding.rows.size()>=32)break;binding.rows.push_back(row);}
                const auto previous=store.Lookup(context.stream);
                const bool resolveAliasFirst=!previous.id.empty()&&binding.id.empty()
                    &&Name(previous.channel)!=Name(binding.channel);
                if(!resolveAliasFirst&&!store.Remember(context.stream,binding))return;
                auto cancel=std::make_shared<std::atomic_bool>(false);auto bytes=GuideBytes(cancel);if(bytes.empty())return;
                HRESULT co=CoInitializeEx(nullptr,COINIT_MULTITHREADED);auto stream=new GzipStream(bytes);
                CNativeProgrammeGuide resolved;resolved.stream=context.stream;
                if(ReadGuide(stream,context.channel,resolved,CNativeProgrammeGuide::Now(),context.xmltvId,&context)){
                    binding.id=resolved.xmltvId;binding.rows.clear();store.Remember(context.stream,binding);
                }else if(resolveAliasFirst){store.Remember(context.stream,binding);}
                stream->Release();if(SUCCEEDED(co))CoUninitialize();
            }catch(...){}
        }).detach();}catch(...){}
    }
    bool NeedsRequest(const std::wstring& url)const{return source!=url||!result;}
    bool Poll(const std::wstring& url,const std::wstring& hint,CNativeProgrammeGuide& guide){
        if(url.empty()){Reset();return false;}
        if(url!=source){Reset();source=url;}
        if(result&&delivered&&GetTickCount64()-requested>(result->guide.rows.empty()?10ULL*60*1000:12ULL*60*60*1000)){Reset();source=url;}
        if(!result){
            result=std::make_shared<Result>();auto state=result;auto input=url;auto known=hint;requested=GetTickCount64();
            try { std::thread([state,input,known]{
                CNativeProgrammeGuide found;found.stream=input;
                try{
                    StreamIdentity::Store store;auto binding=store.Lookup(input);
                    auto name=binding.channel;
                    if(name.empty())name=StreamIdentity::Useful(known)?known:Probe(input,state->cancel);
                    if(!StreamIdentity::Useful(name))name.clear();
                    found.channel=name;found.rows=binding.rows;
                    CNativeProgrammeGuide context=found;
                    if(!name.empty()&&!state->cancel->load()){
                        auto bytes=GuideBytes(state->cancel);
                        if(!bytes.empty()){
                            HRESULT co=CoInitializeEx(nullptr,COINIT_MULTITHREADED);auto stream=new GzipStream(bytes);
                            CNativeProgrammeGuide resolved;resolved.stream=input;
                            if(ReadGuide(stream,name,resolved,CNativeProgrammeGuide::Now(),binding.id,&context)){
                                found=std::move(resolved);
                                StreamIdentity::Binding learned;learned.channel=name;learned.id=found.xmltvId;
                                if(binding.channel.empty()||binding.id.empty())store.Remember(input,learned);
                            }
                            stream->Release();if(SUCCEEDED(co))CoUninitialize();
                        }
                    }
                    if(!found.Current(input,CNativeProgrammeGuide::Now()))found.rows.clear();
                }catch(...){found.Clear();}
                std::lock_guard<std::mutex> lock(state->mutex);state->guide=std::move(found);state->done=true;
            }).detach(); } catch(...) { state->done=true; }
        }
        std::lock_guard<std::mutex> lock(result->mutex);
        if(!result->done||delivered)return false;delivered=true;
        if(result->guide.channel.empty()||result->guide.stream!=url)return false;
        guide=result->guide;return true;
    }
};
}
#endif
