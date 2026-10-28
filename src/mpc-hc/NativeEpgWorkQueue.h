// One active background task and one replaceable pending task. No UI ownership.
#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace NativeStreamEpg {
using Cancellation=std::shared_ptr<std::atomic_bool>;
class LatestWorkQueue {
public:
    using Task=std::function<void(const Cancellation&)>;
private:
    struct State {
        std::mutex mutex;std::condition_variable ready;bool stopping=false;
        Task pending;Cancellation active,pendingCancel;
    };
    std::shared_ptr<State> state=std::make_shared<State>();
public:
    LatestWorkQueue(){
        auto owned=state;
        try{std::thread([owned]{
            for(;;){
                Task task;Cancellation cancel;
                {
                    std::unique_lock<std::mutex> lock(owned->mutex);
                    owned->ready.wait(lock,[&]{return owned->stopping||bool(owned->pending);});
                    if(owned->stopping)return;
                    task.swap(owned->pending);cancel=std::move(owned->pendingCancel);owned->active=cancel;
                }
                try{if(!cancel->load())task(cancel);}catch(...){}
                {std::lock_guard<std::mutex> lock(owned->mutex);owned->active.reset();}
            }
        }).detach();}catch(...){state->stopping=true;}
    }
    LatestWorkQueue(const LatestWorkQueue&)=delete;
    LatestWorkQueue& operator=(const LatestWorkQueue&)=delete;
    ~LatestWorkQueue(){Stop();}
    Cancellation Submit(Task task){
        auto cancel=std::make_shared<std::atomic_bool>(false);
        std::lock_guard<std::mutex> lock(state->mutex);
        if(state->stopping){cancel->store(true);return cancel;}
        if(state->active)state->active->store(true);
        if(state->pendingCancel)state->pendingCancel->store(true);
        state->pending=std::move(task);state->pendingCancel=cancel;state->ready.notify_one();return cancel;
    }
    void Cancel(){
        std::lock_guard<std::mutex> lock(state->mutex);
        if(state->active)state->active->store(true);
        if(state->pendingCancel)state->pendingCancel->store(true);
        state->pending={};state->pendingCancel.reset();
    }
    void Stop(){
        std::lock_guard<std::mutex> lock(state->mutex);state->stopping=true;
        if(state->active)state->active->store(true);
        if(state->pendingCancel)state->pendingCancel->store(true);
        state->pending={};state->pendingCancel.reset();state->ready.notify_one();
    }
};
}
