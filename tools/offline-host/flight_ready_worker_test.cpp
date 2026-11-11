// SPDX-License-Identifier: GPL-3.0-only
// Exercises the actual owner and queue with synthetic input only.
#include "../../src/flight_ready_worker.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>
using namespace std::chrono_literals;
using offline_host::FlightReadyWorker;

int main() {
    int errors = 0;
    auto check = [&](bool ok, const char* name) { if (!ok) { std::printf("FAIL %s\n", name); ++errors; } };
    {
        auto owner = std::make_shared<sco::OwnedShutdown>();
        std::atomic<int> calls{0};
        FlightReadyWorker worker(owner, {[&] { ++calls; return true; }, [] {}, [] {}});
        owner->request_stop();
        check(!worker.request(), "closed owner refuses");
        check(owner->drain_until(std::chrono::steady_clock::now() + 2s), "closed owner drains");
        check(calls == 0, "closed owner sends no input");
    }
    {
        auto owner = std::make_shared<sco::OwnedShutdown>();
        std::atomic<int> launches{0};
        FlightReadyWorker worker(owner, {[] { return true; }, [] {}, [] {}},
            [&](sco::OwnedShutdown&, FlightReadyWorker::Work) { ++launches; return false; });
        check(!worker.request(), "launch failure refuses request");
        check(launches == 1, "launcher invoked");
        owner->request_stop();
        check(owner->drain_until(std::chrono::steady_clock::now() + 2s), "failed launch drains");
    }
    {
        auto owner = std::make_shared<sco::OwnedShutdown>();
        std::mutex mutex;
        std::condition_variable cv;
        bool entered = false, release = false;
        std::atomic<int> downs{0}, ups{0};
        FlightReadyWorker worker(owner, {
            [&] { ++downs; return true; }, [&] { ++ups; }, [&] {
                std::unique_lock lock(mutex); entered = true; cv.notify_all();
                cv.wait(lock, [&] { return release; });
            }});
        check(worker.request(), "first request accepted");
        {
            std::unique_lock lock(mutex);
            check(cv.wait_for(lock, 2s, [&] { return entered; }), "down and hold entered");
        }
        std::atomic<int> accepted{0};
        std::thread competitors[8];
        for (auto& competitor : competitors) competitor = std::thread([&] { if (worker.request()) ++accepted; });
        for (auto& competitor : competitors) competitor.join();
        check(accepted == 0, "concurrent overlapping requests refuse");
        owner->request_stop();
        check(!worker.request(), "closed owner rejects new request");
        std::atomic<bool> drained{false};
        std::thread drain([&] { drained = owner->drain_until(std::chrono::steady_clock::now() + 3s); });
        {
            std::lock_guard lock(mutex);
            check(ups == 0 && !drained, "drain waits during held key");
            release = true;
        }
        cv.notify_all(); drain.join();
        check(drained && downs == 1 && ups == 1, "stop after down pairs keyup before drain");
    }
    {
        auto owner = std::make_shared<sco::OwnedShutdown>();
        std::mutex mutex;
        std::condition_variable cv;
        bool entered = false, release = false;
        std::atomic<int> calls{0};
        FlightReadyWorker worker(owner, {[&] { ++calls; return true; }, [] {}, [] {}},
            [&](sco::OwnedShutdown& o, FlightReadyWorker::Work work) {
                return o.start_worker([&, work = std::move(work)](auto stop) mutable {
                    { std::unique_lock lock(mutex); entered = true; cv.notify_all(); cv.wait(lock, [&] { return release; }); }
                    work(stop);
                });
            });
        check(worker.request(), "delayed worker accepted");
        { std::unique_lock lock(mutex); check(cv.wait_for(lock, 2s, [&] { return entered; }), "delayed worker entered"); }
        owner->request_stop();
        { std::lock_guard lock(mutex); release = true; } cv.notify_all();
        check(owner->drain_until(std::chrono::steady_clock::now() + 3s), "delayed worker drains");
        check(calls == 0, "stop before down suppresses input");
    }
    {
        auto owner = std::make_shared<sco::OwnedShutdown>();
        std::mutex mutex;
        std::condition_variable cv;
        int ups = 0;
        FlightReadyWorker worker(owner, {[] { return true; }, [&] { std::lock_guard lock(mutex); ++ups; cv.notify_all(); }, [] {}});
        bool completed = true;
        for (int i = 1; i <= 70 && completed; ++i) {
            const auto deadline = std::chrono::steady_clock::now() + 2s;
            while (!worker.request() && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
            std::unique_lock lock(mutex);
            completed = cv.wait_until(lock, deadline, [&] { return ups == i; });
        }
        check(completed && ups == 70, "70 successive presses reuse one owner worker");
        owner->request_stop();
        check(owner->drain_until(std::chrono::steady_clock::now() + 3s), "reused worker drains");
    }
    {
        auto owner = std::make_shared<sco::OwnedShutdown>();
        std::mutex mutex;
        std::condition_variable cv;
        int ups = 0;
        FlightReadyWorker worker(owner, {[] { return true; }, [&] { std::lock_guard lock(mutex); ++ups; cv.notify_all(); }, [] { throw 1; }});
        check(worker.request(), "exception test accepted");
        { std::unique_lock lock(mutex); check(cv.wait_for(lock, 2s, [&] { return ups == 1; }), "exception produces keyup"); }
        owner->request_stop();
        check(!owner->drain_until(std::chrono::steady_clock::now() + 3s), "fault retains owner");
        check(ups == 1 && !worker.request(), "exception pairs exactly one keyup and closes admission");
    }
    {
        auto owner = std::make_shared<sco::OwnedShutdown>();
        std::mutex mutex;
        std::condition_variable cv;
        int attemptedUps = 0;
        FlightReadyWorker worker(owner, {[] { return true; }, [&] {
            { std::lock_guard lock(mutex); ++attemptedUps; cv.notify_all(); }
            throw 1;
        }, [] {}});
        check(worker.request(), "keyup failure test accepted");
        { std::unique_lock lock(mutex); check(cv.wait_for(lock, 2s, [&] { return attemptedUps == 1; }), "keyup attempted"); }
        owner->request_stop();
        check(!owner->drain_until(std::chrono::steady_clock::now() + 3s), "keyup failure retains owner");
        check(attemptedUps == 1 && !worker.request(), "keyup failure is not retried and closes admission");
    }
    std::printf("flight-ready owner worker: %d failure(s)\n", errors);
    return errors ? 1 : 0;
}
