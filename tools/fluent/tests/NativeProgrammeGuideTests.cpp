#define NOMINMAX
#include "NativeProgrammeGuide.h"
#include <iostream>
#include <cstdlib>
static int count=0;
static void check(bool ok,const char* what){++count;if(!ok){std::cerr<<"FAIL "<<what<<"\n";std::exit(1);}}
int main(){
    using G=CNativeProgrammeGuide;
    const auto begin=G::Stamp(L"20261021165000 +0300"),end=G::Stamp(L"20261021183000 +0300");
    check(begin!=0&&end>begin,"XMLTV timestamps");
    check(begin==G::Stamp(L"20261021135000 +0000"),"Moscow to UTC");
    check(begin==G::Stamp(L"20261021185000 +0500"),"Qyzylorda same instant");
    check(G::Stamp(L"20261021233000 +0300")==G::Stamp(L"20261022013000 +0500"),"midnight rollover");
    check(!G::Stamp(L"20261021165000"),"reject unknown timezone");
    check(!G::Stamp(L"20261021165000 +0360"),"invalid zone minute");
    check(!G::Stamp(L"20260230165000 +0300"),"invalid calendar date");
    G g;
    const std::wstring json=LR"({"version":1,"stream":"https://example.org/live.m3u8","context":{"channelName":"Channel","programmes":[{"title":"Film","startXml":"20261021165000 +0300","stopXml":"20261021183000 +0300"},{"title":"Next","startXml":"20261021183000 +0300","stopXml":"20261021200500 +0300"}]}})";
    check(g.Parse(json,L"https://example.org/live.m3u8"),"generic imported guide");
    check(g.Current(g.stream,begin)&&g.Current(g.stream,begin)->title==L"Film","inclusive start");
    check(g.Current(g.stream,end)&&g.Current(g.stream,end)->title==L"Next","exclusive stop and next programme");
    check(!g.Current(L"https://other.example/live.m3u8",begin),"exact source binding");
    check(!g.Current(L"C:\\video\\live.m3u8",begin),"local file cannot match");
    check(!g.Current(g.stream,begin-1),"future guide fallback");
    check(!g.Current(g.stream,end+864000000000ULL),"expired guide fallback");
    check(g.Tooltip(g.rows[0]).find(L" - ")!=std::wstring::npos,"ASCII minus in tooltip");
    check(g.Tooltip(g.rows[0]).find(L" / Channel")!=std::wstring::npos,"channel in tooltip");
    check(g.Remaining(begin).size()==2,"remaining schedule includes current and next");
    check(g.Remaining(end).size()==1,"past programme excluded");
    {
        G schedule;
        schedule.rows = {{begin-20, begin, L"Expired"}, {begin, end, L"Current"},
            {end+864000000000ULL, end+900000000000ULL, L"Tomorrow"}};
        const auto remaining = schedule.Remaining(begin);
        check(remaining.size()==2 && remaining[0].title==L"Current" && remaining[1].title==L"Tomorrow",
            "retain future dates while excluding expired rows");
        check(schedule.Remaining(end).size()==1, "retain tomorrow after current stop");
    }
    {
        std::wstring title, tip;
        check(G::LabelTitle(L"  13:00 Programme") == L"Programme", "strip leading schedule clock from fallback title");
        check(G::LabelTitle(L"13:00 - 14:00 Programme") == L"Programme", "strip schedule range from fallback title");
        check(G::LabelTitle(L"12:01") == L"12:01", "retain a title that is itself a clock");
        check(G::LabelTitle(L"Movie 13:00") == L"Movie 13:00", "retain embedded time in a title");
        check(G::LabelTitle(L"25:99 Programme") == L"25:99 Programme", "do not strip invalid clock prefixes");
        G noGuide;
        check(noGuide.Caption(L"https://other.example/live.m3u8", L"13:00 Programme", begin, true, title, tip)
            && title==L"Programme" && tip==L"13:00 Programme",
            "missing structured guide separates title text from full playlist tooltip");

        check(g.Caption(g.stream,L"Channel",begin,true,title,tip) && title==L"Film" && tip==g.Tooltip(g.rows[0]),
            "startup notification uses current programme and existing titlebar tooltip");
        const auto nextLabel=G::Clock(g.rows[1].start)+L" "+g.rows[1].title;
        check(g.Caption(g.stream,nextLabel,begin,false,title,tip) && title==L"Next" && tip==g.Tooltip(g.rows[1]),
            "hovered future row describes that programme rather than current broadcast");
        check(g.Caption(g.stream,L"Channel",begin,false,title,tip) && tip==L"Channel",
            "channel row tooltip contains channel name");
        check(g.Caption(g.stream,L"Channel",end+864000000000ULL,true,title,tip) && tip==L"Channel",
            "expired guide falls back to channel without source address");
        check(!g.Caption(L"C:\\video\\film.mp4",L"Film",begin,true,title,tip),
            "local-file notification and tooltip stay native");
        check(g.Caption(L"https://other.example/index.m3u8?token=private",L"index.m3u8?token=private",begin,true,title,tip)
            && title.empty() && tip.empty(), "unlabelled signed URL is suppressed");
        check(g.Caption(L"https://other.example/index.m3u8?token=private",L"index",begin,false,title,tip)
            && tip.empty(), "URL filename stem is not presented as a title");
        check(g.Caption(L"https://other.example/index.m3u8?token=private",L"Imported stream title",begin,true,title,tip)
            && title==L"Imported stream title" && tip==title, "distinct imported stream labels are retained");
        check(g.Caption(L"https://other.example/index.m3u8",L"https://other.example/index.m3u8",begin,false,title,tip)
            && tip.empty(), "raw source labels are not repeated in a tooltip");
    }
    {
        G channelOnly;
        const std::wstring partial=LR"({"version":1,"stream":"https://example.org/live.m3u8","context":{"channelName":"Channel","programmes":[]}})";
        check(channelOnly.Parse(partial,L"https://example.org/live.m3u8"), "retain source-bound channel with label-only schedule");
        check(channelOnly.rows.empty() && channelOnly.HasChannelHeader(channelOnly.stream,L"Channel"),
            "channel header identity does not require EPG intervals");
        check(!channelOnly.HasChannelHeader(channelOnly.stream,L"13:00 Programme")
            && !channelOnly.HasChannelHeader(L"https://other.example/live.m3u8",L"Channel"),
            "do not mistake a programme or another source for a channel header");
        std::wstring title, tip;
        check(channelOnly.Caption(channelOnly.stream,L"13:00 Programme",begin,true,title,tip)
            && title==L"Programme" && tip==L"13:00 Programme",
            "retaining channel metadata must not replace fallback programme title with channel name");
        check(channelOnly.Caption(channelOnly.stream,L"Channel",begin,false,title,tip) && tip==L"Channel",
            "channel-only tooltip retains channel identity");
    }
    g.rows.push_back(g.rows[0]);
    check(!g.Current(g.stream,begin),"overlapping guide rejected");
    check(!g.Parse(json,L"https://other.example/live.m3u8")&&g.rows.empty(),"mismatch clears old metadata");
    check(!g.Parse(L"{",L"https://example.org/live.m3u8"),"malformed metadata");
    check(!g.Parse(json,L"C:\\video\\film.mp4"),"local media rejects EPG import");
    {
        std::ofstream f("programme-guide-fixture.json",std::ios::binary);
        const std::string utf8(json.begin(),json.end()); f.write(utf8.data(),utf8.size());
    }
    check(g.Load(L"programme-guide-fixture.json",L"https://example.org/live.m3u8"),"UTF8 sidecar import");
    {
        std::ofstream f("programme-guide-fixture.json",std::ios::binary);
        f.write("\xff\xfe",2); f.write(reinterpret_cast<const char*>(json.data()),json.size()*sizeof(wchar_t));
    }
    check(g.Load(L"programme-guide-fixture.json",L"https://example.org/live.m3u8"),"VBScript UTF16 sidecar import");
    std::remove("programme-guide-fixture.json");
    check(g.Parse(LR"({"version":1,"stream":"https://second.example/hls","context":{"programmes":[{"title":"Other provider","start":1792590600000,"stop":1792596600000}]}})",L"https://second.example/hls"),"independent provider numeric epoch metadata");
    std::cout<<"PASS "<<count<<" native programme guide checks\n";
}
