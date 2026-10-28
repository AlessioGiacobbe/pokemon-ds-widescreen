#pragma once
// Runs the emulation of a frame on a thread of its own, so that the main thread can show the
// frame before at the same time (the pipeline in main.cpp): putting a frame together on the
// graphics card and presenting it took about 5 ms of each frame on the Switch, on the same
// core as the emulation. On the Switch the emulation thread has core 0 to itself (the main
// thread moves to core 2) and a stack as big as the main thread's.
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#ifdef __SWITCH__
#include "switch_platform.h"
#endif

class EmuThread
{
public:
    ~EmuThread() { Stop(); }

    // job runs on the thread once for each Kick.
    bool Start(std::function<void()> job)
    {
        if (Running) return true;
        Job = std::move(job);
        Quit = Pending = false;
#ifdef __SWITCH__
        if (!SwitchPlatform::StartThread(Entry, this)) return false;
#else
        T = std::thread([this] { Loop(); });
#endif
        Running = true;
        return true;
    }

    void Kick()
    {
        std::lock_guard<std::mutex> lock(M);
        Pending = true;
        CV.notify_all();
    }

    // Until the job started by the last Kick has finished.
    void Wait()
    {
        std::unique_lock<std::mutex> lock(M);
        CV.wait(lock, [this] { return !Pending; });
    }

    // Until the job started by the last Kick has finished or `seconds` have passed; whether it
    // has finished.
    bool WaitFor(double seconds)
    {
        std::unique_lock<std::mutex> lock(M);
        return CV.wait_for(lock, std::chrono::duration<double>(seconds), [this] { return !Pending; });
    }

    // Until the job started by the last Kick has finished, calling service() meanwhile (every
    // fraction of a millisecond), for work the job hands to this thread.
    template <typename F> void WaitServicing(F service)
    {
        std::unique_lock<std::mutex> lock(M);
        while (Pending)
        {
            lock.unlock();
            service();
            lock.lock();
            if (Pending) CV.wait_for(lock, std::chrono::microseconds(250), [this] { return !Pending; });
        }
    }

    void Stop()
    {
        if (!Running) return;
        Wait();
        {
            std::lock_guard<std::mutex> lock(M);
            Quit = true;
            CV.notify_all();
        }
#ifdef __SWITCH__
        SwitchPlatform::JoinThread();
#else
        T.join();
#endif
        Running = false;
    }

    bool IsRunning() const { return Running; }

private:
    void Loop()
    {
        std::unique_lock<std::mutex> lock(M);
        for (;;)
        {
            CV.wait(lock, [this] { return Pending || Quit; });
            if (Quit) return;
            lock.unlock();
            Job();
            lock.lock();
            Pending = false;
            CV.notify_all();
        }
    }

#ifdef __SWITCH__
    static void Entry(void* self) { static_cast<EmuThread*>(self)->Loop(); }
#else
    std::thread T;
#endif
    std::function<void()> Job;
    std::mutex M;
    std::condition_variable CV;
    bool Pending = false, Quit = false, Running = false;
};
