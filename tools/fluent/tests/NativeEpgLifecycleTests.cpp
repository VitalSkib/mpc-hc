#define NOMINMAX
#include "NativeStreamEpg.h"
#include <future>
#include <iostream>
#include <cstdlib>
#include <sstream>
static int checks=0;
static void check(bool ok,const char* text){++checks;if(!ok){std::cerr<<"FAIL "<<text<<"\n";std::exit(1);}}
struct Gate {
    std::mutex mutex;std::condition_variable changed;bool entered=false,release=false;
    std::atomic_bool observed{false};std::atomic_int unexpected{0};std::promise<void> completed;
};
static void entered(const std::shared_ptr<Gate>& gate){
    std::unique_lock<std::mutex> lock(gate->mutex);
    check(gate->changed.wait_for(lock,std::chrono::seconds(10),[&]{return gate->entered;}),"worker starts without UI pumping");
}
static void release(const std::shared_ptr<Gate>& gate){std::lock_guard<std::mutex> lock(gate->mutex);gate->release=true;gate->changed.notify_all();}
static std::vector<unsigned char> read(const std::wstring& file){std::ifstream f(file.c_str(),std::ios::binary);return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};}
static void write(const std::wstring& file,const std::vector<unsigned char>& bytes){std::ofstream f(file.c_str(),std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());}
struct Counted {int* releases;void Release(){++*releases;delete this;}};
int main(){
    using namespace NativeStreamEpg;
    {
        LatestWorkQueue queue;auto gate=std::make_shared<Gate>();auto done=gate->completed.get_future();
        auto first=queue.Submit([gate](const Cancellation& cancel){
            std::unique_lock<std::mutex> lock(gate->mutex);gate->entered=true;gate->changed.notify_all();
            gate->changed.wait(lock,[&]{return gate->release;});gate->observed=cancel->load();
        });entered(gate);
        auto second=queue.Submit([gate](const Cancellation&){++gate->unexpected;});
        auto last=queue.Submit([gate](const Cancellation&){gate->completed.set_value();});
        check(first->load()&&second->load()&&!last->load(),"superseded active and pending work cancelled");
        release(gate);check(done.wait_for(std::chrono::seconds(10))==std::future_status::ready,"latest task completes");
        check(gate->observed&&gate->unexpected==0,"only active and latest tasks run");
        queue.Stop();auto rejected=queue.Submit([gate](const Cancellation&){++gate->unexpected;});
        check(rejected->load(),"stopped queue rejects new work");
    }
    {
        auto gate=std::make_shared<Gate>();auto done=gate->completed.get_future();Cancellation active,pending;
        {
            LatestWorkQueue queue;
            active=queue.Submit([gate](const Cancellation& cancel){
                std::unique_lock<std::mutex> lock(gate->mutex);gate->entered=true;gate->changed.notify_all();
                gate->changed.wait(lock,[&]{return gate->release;});gate->observed=cancel->load();gate->completed.set_value();
            });entered(gate);
            pending=queue.Submit([gate](const Cancellation&){++gate->unexpected;});
        }
        check(active->load()&&pending->load(),"destruction cancels work without waiting for I/O");
        release(gate);check(done.wait_for(std::chrono::seconds(10))==std::future_status::ready,"owned state survives until active task exits");
        check(gate->observed&&gate->unexpected==0,"destroyed owner cannot run pending callback");
    }
    int released=0;try{ComOwner<Counted> owner(new Counted{&released});throw 1;}catch(...){}
    check(released==1,"COM release is scoped across exceptions");
    ULONG_PTR context=0;check(CoGetContextToken(&context)==CO_E_NOTINITIALIZED,"test starts without COM apartment");
    try{ComApartment apartment;check(apartment.Ready(),"scoped COM initialisation");throw 1;}catch(...){}
    check(CoGetContextToken(&context)==CO_E_NOTINITIALIZED,"COM apartment balanced on exception");
    std::vector<unsigned char> oldGuide={31,139,8,0,0,0,0,0,2,255,85,141,65,10,195,32,16,69,175,34,110,75,113,98,74,86,147,217,244,0,61,131,212,33,21,212,4,157,4,122,251,54,68,66,187,24,248,188,199,255,131,178,17,62,95,46,103,142,42,248,81,11,87,209,132,62,212,37,186,247,53,187,196,116,63,60,154,63,138,166,245,8,151,50,79,197,165,196,170,161,182,163,170,184,34,163,182,96,135,110,191,254,214,1,168,11,244,0,187,156,151,31,55,128,61,29,161,4,137,76,143,232,213,180,6,207,104,14,128,230,252,245,205,178,209,7,181,80,5,239,193,0,0,0};
    std::vector<unsigned char> newGuide={31,139,8,0,0,0,0,0,2,255,85,141,65,10,194,48,16,69,175,18,178,21,201,52,149,174,166,179,113,239,29,130,25,106,32,73,67,58,86,188,189,150,134,162,139,129,207,123,252,63,40,43,225,253,225,114,230,168,130,31,181,240,34,154,208,135,165,68,247,62,103,151,152,174,187,71,243,71,209,180,30,97,169,243,84,93,74,172,26,106,59,106,17,87,101,212,22,236,208,109,215,95,58,0,117,130,30,96,147,115,249,113,3,216,195,17,74,144,200,116,227,151,154,158,193,51,154,29,160,57,126,125,179,172,244,1,255,118,214,69,193,0,0,0};
    std::vector<unsigned char> notXml={31,139,8,0,0,0,0,0,2,255,179,201,47,201,72,45,210,183,3,0,197,72,191,123,8,0,0,0};
    auto cancel=std::make_shared<std::atomic_bool>(false);
    check(ValidateGuide(oldGuide,cancel),"complete gzip and XMLTV accepted");
    check(CoGetContextToken(&context)==CO_E_NOTINITIALIZED,"validation leaves no COM apartment behind");
    check(!ValidateGuide(notXml,cancel),"valid gzip with wrong XML root rejected");
    auto truncated=oldGuide;truncated.resize(truncated.size()-5);check(!ValidateGuide(truncated,cancel),"truncated gzip rejected");
    auto crc=oldGuide;crc[crc.size()-8]^=1;check(!ValidateGuide(crc,cancel),"gzip CRC mismatch rejected");
    cancel->store(true);check(!ValidateGuide(oldGuide,cancel),"validation observes cancellation");
    {ComOwner<GzipStream> stream(new GzipStream(oldGuide,cancel));BYTE block[32];ULONG got=0;check(stream->Read(block,sizeof(block),&got)==E_ABORT,"gzip reading observes cancellation");}
    cancel->store(false);
    wchar_t temp[MAX_PATH];check(GetTempPathW(MAX_PATH,temp)!=0,"temporary path available");
    auto folder=std::wstring(temp)+L"MPCEpgLifecycle."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetTickCount64());
    check(CreateDirectoryW(folder.c_str(),nullptr)!=0,"isolated cache directory created");auto file=folder+L"\\guide.xml.gz";write(file,oldGuide);
    const auto now=CNativeProgrammeGuide::Now()+10ULL*10000000;int downloads=0;
    auto invalid=[&](const Cancellation&){++downloads;return std::vector<unsigned char>{0x1f,0x8b};};
    check(LoadGuideCache(file,cancel,invalid,now)==oldGuide&&downloads==0,"fresh verified cache avoids network");
    check(LoadGuideCache(file,cancel,invalid,now+13ULL*60*60*10000000)==oldGuide&&downloads==1,"bad update falls back to last valid cache");
    check(read(file)==oldGuide,"bad download never replaces disk cache");
    auto valid=[&](const Cancellation&){return newGuide;};
    check(LoadGuideCache(file,cancel,valid,now+13ULL*60*60*10000000)==newGuide&&read(file)==newGuide,"verified update replaces cache atomically");
    auto abort=[&](const Cancellation& token){token->store(true);return oldGuide;};
    check(LoadGuideCache(file,cancel,abort,now+13ULL*60*60*10000000).empty()&&read(file)==newGuide,"cancelled update leaves disk cache intact");
    cancel->store(false);check(DeleteFileW(file.c_str())&&RemoveDirectoryW(folder.c_str()),"cache test files released and removed");
    CNativeProgrammeGuide guide;guide.stream=L"https://example.test/index.m3u8";guide.rows={{100,200,L"First"},{200,300,L"Next"}};
    check(guide.RemainingKey(100)==guide.RemainingKey(199),"no list rebuild within one programme");
    check(guide.RemainingKey(199)!=guide.RemainingKey(200),"list revision changes at programme boundary");
    check(guide.Remaining(200).size()==1&&guide.Current(guide.stream,200)->title==L"Next","list and caption select the same next programme");
    check(guide.RemainingKey(300)==std::make_pair(0ULL,0ULL)&&guide.Remaining(300).empty(),"last programme expiry removes old rows");
    const auto midnight=CNativeProgrammeGuide::Stamp(L"20261028000000 +0300");guide.rows={{midnight-600000000,midnight,L"Before"},{midnight,midnight+600000000,L"After"}};
    check(guide.RemainingKey(midnight-1)!=guide.RemainingKey(midnight),"rollover crosses midnight without day filtering");
    std::cout<<checks<<" lifecycle checks passed\n";
}
