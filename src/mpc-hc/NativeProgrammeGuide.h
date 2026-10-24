// Structured, source-bound programme metadata. GPLv3+.
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <algorithm>
#include <fstream>
#include <cmath>
#include "rapidjson/include/rapidjson/document.h"

class CNativeProgrammeGuide {
public:
    struct Programme { ULONGLONG start = 0, stop = 0; std::wstring title; };
    std::wstring stream, channel;
    std::vector<Programme> rows;
    void Clear() { stream.clear(); channel.clear(); rows.clear(); }
    static ULONGLONG Now() { FILETIME f; GetSystemTimeAsFileTime(&f); return (ULONGLONG(f.dwHighDateTime)<<32)|f.dwLowDateTime; }
    static bool Local(ULONGLONG ticks, SYSTEMTIME& local) {
        FILETIME f{DWORD(ticks),DWORD(ticks>>32)}; SYSTEMTIME utc{};
        return FileTimeToSystemTime(&f,&utc) && SystemTimeToTzSpecificLocalTimeEx(nullptr,&utc,&local);
    }
    static std::wstring Clock(ULONGLONG ticks) {
        SYSTEMTIME s{}; if (!Local(ticks,s)) return {};
        wchar_t b[8]; swprintf_s(b,L"%02u:%02u",s.wHour,s.wMinute); return b;
    }
    static std::wstring Clean(std::wstring s) {
        for (auto& c:s) if(c==L'\r'||c==L'\n'||c==L'\t') c=L' ';
        const auto first=s.find_first_not_of(L' '); if(first==s.npos)return {};
        s=s.substr(first,s.find_last_not_of(L' ')-first+1);
        for(const auto prefix:{L"х/ф ",L"д/ф ",L"т/с ",L"м/ф "}) {
            if(s.size()>=4 && _wcsnicmp(s.c_str(),prefix,4)==0) {s.erase(0,4); break;}
        }
        return s;
    }
    // XMLTV requires an explicit timezone here. Bare wall clocks are ambiguous.
    static ULONGLONG Stamp(const std::wstring& s) {
        if(s.size()!=20 || s[14]!=L' ' || (s[15]!=L'+'&&s[15]!=L'-')) return 0;
        auto number=[&](size_t at,size_t count) {int n=0;for(size_t i=at;i<at+count;++i){if(s[i]<L'0'||s[i]>L'9')return -1;n=n*10+s[i]-L'0';}return n;};
        int y=number(0,4),mo=number(4,2),d=number(6,2),h=number(8,2),mi=number(10,2),sec=number(12,2),oh=number(16,2),om=number(18,2);
        if(y<1970||mo<1||mo>12||d<1||d>31||h<0||h>23||mi<0||mi>59||sec<0||sec>59||oh<0||oh>14||om<0||om>59||(oh==14&&om))return 0;
        SYSTEMTIME st{};st.wYear=WORD(y);st.wMonth=WORD(mo);st.wDay=WORD(d);st.wHour=WORD(h);st.wMinute=WORD(mi);st.wSecond=WORD(sec);
        FILETIME f{}; if(!SystemTimeToFileTime(&st,&f))return 0;
        LONGLONG ticks=(ULONGLONG(f.dwHighDateTime)<<32)|f.dwLowDateTime;
        ticks-=(s[15]==L'+'?1LL:-1LL)*(oh*60+om)*600000000LL;
        return ticks>0?ULONGLONG(ticks):0;
    }
    bool HasChannelHeader(const std::wstring& source, const std::wstring& label) const {
        return !stream.empty() && source==stream && !channel.empty() && label==channel;
    }
    const Programme* Current(const std::wstring& source,ULONGLONG now) const {
        if(source!=stream)return nullptr;
        const Programme* found=nullptr;
        for(const auto& p:rows) if(p.start<=now&&now<p.stop){if(found)return nullptr;found=&p;}
        return found; // Reject conflicting overlapping intervals.
    }
    std::vector<Programme> Remaining(ULONGLONG now) const {
        std::vector<Programme> out;
        // Absolute timestamps retain the source order across local midnight.
        for (const auto& p : rows) if (p.stop > now) out.push_back(p);
        return out;
    }
    std::wstring Tooltip(const Programme& p) const {
        return Clock(p.start)+L" - "+Clock(p.stop)+L" \u00ab"+p.title+L"\u00bb"+(channel.empty()?L"":L" / "+channel);
    }
    static std::wstring LabelTitle(std::wstring label) {
        label=Clean(label);
        const auto clock=[](const std::wstring& text,size_t pos) {
            if(pos+5>text.size() || text[pos+2]!=L':') return false;
            for(const size_t i:{pos,pos+1,pos+3,pos+4}) if(text[i]<L'0'||text[i]>L'9') return false;
            return (text[pos]-L'0')*10+text[pos+1]-L'0'<24
                && (text[pos+3]-L'0')*10+text[pos+4]-L'0'<60;
        };
        const auto space=[](wchar_t c){return c==L' '||c==0x00a0;};
        if(!clock(label,0) || label.size()<=5 || !space(label[5])) return label;
        size_t begin=5; while(begin<label.size() && space(label[begin])) ++begin;
        if(begin<label.size() && (label[begin]==L'-'||label[begin]==0x2013||label[begin]==0x2014)) {
            size_t end=begin+1; while(end<label.size() && space(label[end])) ++end;
            if(clock(label,end) && end+5<label.size() && space(label[end+5])) {
                begin=end+5; while(begin<label.size() && space(label[begin])) ++begin;
            }
        }
        const auto title=Clean(label.substr(begin));
        return title.empty()?label:title;
    }
    // Presentation-only text for the two source-name notifications. A handled
    // HTTP source with empty text intentionally suppresses an unlabelled URL.
    bool Caption(const std::wstring& source, const std::wstring& label, ULONGLONG now,
            bool current, std::wstring& title, std::wstring& tip) const {
        title.clear(); tip.clear();
        if (_wcsnicmp(source.c_str(), L"http://", 7) != 0
                && _wcsnicmp(source.c_str(), L"https://", 8) != 0) return false;
        if (source == stream) {
            if (current) {
                if (const auto p = Current(source, now)) { title=p->title; tip=Tooltip(*p); return true; }
                if (!channel.empty() && (!rows.empty() || label==channel)) { title=tip=channel; return true; }
            } else {
                if (!channel.empty() && label == channel) { title=tip=channel; return true; }
                for (const auto& p : rows) if (label == Clock(p.start)+L" "+p.title) {
                    title=p.title; tip=Tooltip(p); return true;
                }
            }
        }
        const auto equal=[](const std::wstring& a,const std::wstring& b) {return _wcsicmp(a.c_str(),b.c_str())==0;};
        const auto cut=source.find_first_of(L"?#");
        const auto clean=source.substr(0,cut);
        const auto slash=clean.find_last_of(L'/');
        const auto file=slash==clean.npos?clean:clean.substr(slash+1);
        const auto dot=file.find_last_of(L'.');
        const auto stem=dot==file.npos?file:file.substr(0,dot);
        const auto rawSlash=source.find_last_of(L'/');
        const auto rawFile=rawSlash==source.npos?source:source.substr(rawSlash+1);
        const auto name=Clean(label);
        if (!name.empty() && name.find(L"://")==name.npos && !equal(name,source)
                && !equal(name,clean) && !equal(name,file) && !equal(name,stem) && !equal(name,rawFile))
        {
            title=LabelTitle(name); tip=name;
        }
        return true;
    }
    bool Parse(const std::wstring& json,const std::wstring& expected) {
        Clear();
        if(_wcsnicmp(expected.c_str(),L"http://",7)!=0&&_wcsnicmp(expected.c_str(),L"https://",8)!=0)return false;
        using Doc=rapidjson::GenericDocument<rapidjson::UTF16<wchar_t>>;
        Doc d; d.Parse(json.c_str());
        if(d.HasParseError()||!d.IsObject()||!d.HasMember(L"version")||!d[L"version"].IsInt()||d[L"version"].GetInt()!=1
            ||!d.HasMember(L"stream")||!d[L"stream"].IsString()||expected!=d[L"stream"].GetString()
            ||!d.HasMember(L"context")||!d[L"context"].IsObject())return false;
        const auto& c=d[L"context"];
        if(!c.HasMember(L"programmes")||!c[L"programmes"].IsArray()||c[L"programmes"].Size()>4096)return false;
        if(c.HasMember(L"channelName")&&c[L"channelName"].IsString())channel=Clean(c[L"channelName"].GetString());
        for(const auto& v:c[L"programmes"].GetArray()) {
            if(!v.IsObject()||!v.HasMember(L"title")||!v[L"title"].IsString())continue;
            auto time=[&](const wchar_t* key,const wchar_t* xml) -> ULONGLONG {
                if(v.HasMember(xml)&&v[xml].IsString())return Stamp(v[xml].GetString());
                if(!v.HasMember(key))return 0;
                if(v[key].IsString())return Stamp(v[key].GetString());
                if(!v[key].IsNumber())return 0;
                const double ms=v[key].GetDouble();
                if(!std::isfinite(ms)||ms<0||ms>32503680000000.0)return 0;
                return 116444736000000000ULL+ULONGLONG(ms*10000.0);
            };
            Programme p{time(L"start",L"startXml"),time(L"stop",L"stopXml"),Clean(v[L"title"].GetString())};
            if(p.start&&p.stop>p.start&&!p.title.empty()&&p.title.size()<=1024)rows.push_back(p);
        }
        std::sort(rows.begin(),rows.end(),[](const Programme&a,const Programme&b){return a.start<b.start;});
        rows.erase(std::unique(rows.begin(),rows.end(),[](const Programme&a,const Programme&b){return a.start==b.start&&a.stop==b.stop&&a.title==b.title;}),rows.end());
        // Channel identity is valid independently of dated programme intervals.
        // Keep it for native playlists whose schedule exists only as row labels.
        if(rows.empty() && channel.empty()){Clear();return false;}
        stream=expected;return true;
    }
    bool Load(const std::wstring& path,const std::wstring& expected) {
        Clear();std::ifstream f(path.c_str(),std::ios::binary);if(!f)return false;
        f.seekg(0,std::ios::end);const auto size=f.tellg();if(size<=0||size>2097152)return false;f.seekg(0);
        std::string bytes(size_t(size),'\0');if(!f.read(bytes.data(),size))return false;
        std::wstring text;
        if(bytes.size()>=2&&BYTE(bytes[0])==255&&BYTE(bytes[1])==254) {
            if(bytes.size()%2)return false;text.resize((bytes.size()-2)/2);memcpy(text.data(),bytes.data()+2,bytes.size()-2);
        } else {
            size_t off=bytes.compare(0,3,"\xef\xbb\xbf")==0?3:0;
            const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data()+off,int(bytes.size()-off),nullptr,0);if(!n)return false;
            text.resize(n);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data()+off,int(bytes.size()-off),text.data(),n);
        }
        return Parse(text,expected);
    }
};
