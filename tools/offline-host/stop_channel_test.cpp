// SPDX-License-Identifier: GPL-3.0-only
// The launcher's Stop channel (src/stop_channel.h) with a fake exit sequence. The fake has the shape of the
// product's: Core's closure is latched (a repeat is Ok and does nothing) and the wallet's exit save is latched,
// as in src/contracts.cpp. The real wallet and Core are not linked here; what is proved is that the channel runs
// the work once, acknowledges after it, and that a later quit through the latched path repeats nothing.
#include "stop_channel.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

namespace sc = sc_offline::stop_channel;

namespace {
struct Fake {
    unsigned closures = 0, gameExit = 0, walletSaves = 0;
    bool closed = false, walletRan = false;
    std::vector<std::string> log;
    // Core's Stop: latched, so the quit hook after a launcher stop does nothing.
    void CoreStop() {
        if (closed) return;
        closed = true;
        ++closures;
        ++gameExit;
        SaveWalletAtExit();   // the game.exit subscriber
        log.push_back("[app] process-retained stop: admission closed, backing retained");
    }
    void SaveWalletAtExit() {
        if (walletRan) return;
        walletRan = true;
        ++walletSaves;
        log.push_back("[contracts] wallet saved at exit");
    }
    bool refuseClosure = false;   // Core's gate is busy on this stack: its closure does not run
    // The product's own sequence (sc::RunExitWork, used by RunLauncherStopWork in src/dllmain.cpp) over the fake.
    struct Ops {
        Fake& f;
        void Log(const char* line) { f.log.push_back(line); }
        void CloseHost() { if (!f.refuseClosure) f.CoreStop(); }
        bool HostClosed() { return f.closed; }
        void SaveWallet() { f.SaveWalletAtExit(); }
    };
    void LauncherWork() { Ops ops{*this}; sc::RunExitWork(ops); }
    unsigned Count(const char* line) const {
        unsigned n = 0;
        for (const auto& l : log) n += l == line;
        return n;
    }
};

// A pid no real process of this test run uses for its own events.
DWORD TestPid(unsigned salt) { return 0x7e000000u + (GetCurrentProcessId() & 0xffffu) * 16 + salt; }
}  // namespace

int main() {
    // Names: session-local, per process.
    wchar_t name[64];
    sc::EventName(name, L"stop", 4242);
    CHECK(std::wstring(name) == L"Local\\sc-offline-stop-4242");
    sc::EventName(name, L"stopped", 4242);
    CHECK(std::wstring(name) == L"Local\\sc-offline-stopped-4242");

    // No mod in the process: nothing to open, and Poll without a channel does nothing.
    {
        const DWORD pid = TestPid(0);
        const sc::Request r = sc::RequestStop(pid, 50);
        CHECK(r.outcome == sc::Outcome::NotPresent && r.elapsedMs == 0);
        sc::HostChannel idle;
        unsigned runs = 0;
        CHECK(!idle.Poll([&] { ++runs; }) && runs == 0 && !idle.Created());
    }

    // Created, not signalled: the work does not run and the stopped event stays clear.
    {
        const DWORD pid = TestPid(1);
        sc::HostChannel host;
        CHECK(host.Create(pid));
        Fake fake;
        CHECK(!host.Poll([&] { fake.LauncherWork(); }) && fake.closures == 0 && !host.Done());
        // The launcher gives up after the timeout when nothing answers, and the signal stays set.
        const sc::Request r = sc::RequestStop(pid, 150);
        CHECK(r.outcome == sc::Outcome::TimedOut && r.elapsedMs >= 100);
        CHECK(host.Requested() && !host.Done());
        // The game thread then sees the request on its next tick: work once, acknowledged afterwards.
        CHECK(host.Poll([&] { fake.LauncherWork(); }));
        CHECK(host.Done() && fake.closures == 1 && fake.gameExit == 1 && fake.walletSaves == 1);
        CHECK(fake.Count("[app] stop requested by the launcher: exit work done") == 1);
        wchar_t n[64];
        sc::EventName(n, L"stopped", pid);
        HANDLE stopped = OpenEventW(SYNCHRONIZE, FALSE, n);
        CHECK(stopped && WaitForSingleObject(stopped, 0) == WAIT_OBJECT_0);
        CloseHandle(stopped);

        // Signalled again, or polled again: nothing repeats.
        CHECK(!host.Poll([&] { fake.LauncherWork(); }));
        // The game's own quit hook afterwards: Core's latch and the wallet's latch absorb it.
        fake.CoreStop();
        fake.SaveWalletAtExit();
        CHECK(fake.closures == 1 && fake.gameExit == 1 && fake.walletSaves == 1);
        CHECK(fake.Count("[contracts] wallet saved at exit") == 1);
        CHECK(fake.Count("[app] process-retained stop: admission closed, backing retained") == 1);
        CHECK(fake.Count("[app] stop requested by the launcher: exit work done") == 1);
    }

    // The other order: a real quit first, then a launcher stop. The work still runs once; the latches hold.
    {
        const DWORD pid = TestPid(2);
        sc::HostChannel host;
        CHECK(host.Create(pid));
        Fake fake;
        fake.CoreStop();
        fake.SaveWalletAtExit();
        std::thread launcher([&] { CHECK(sc::RequestStop(pid, 5000).outcome == sc::Outcome::Acknowledged); });
        unsigned polls = 0;
        while (!host.Poll([&] { fake.LauncherWork(); })) {
            CHECK(++polls < 5000);
            Sleep(1);
        }
        launcher.join();
        CHECK(fake.closures == 1 && fake.walletSaves == 1 && fake.Count("[contracts] wallet saved at exit") == 1);
        CHECK(fake.Count("[app] stop requested by the launcher: exit work done") == 1);
    }

    // A tick that re-enters the poll from inside the work (a plugin pumping messages) does not run it twice.
    {
        const DWORD pid = TestPid(3);
        sc::HostChannel host;
        CHECK(host.Create(pid));
        Fake fake;
        unsigned nested = 0;
        std::thread launcher([&] { CHECK(sc::RequestStop(pid, 5000).outcome == sc::Outcome::Acknowledged); });
        for (unsigned polls = 0; !host.Done(); ++polls) {
            CHECK(polls < 5000);
            host.Poll([&] {
                fake.LauncherWork();
                if (host.Poll([&] { ++nested; })) ++nested;
            });
            Sleep(1);
        }
        launcher.join();
        CHECK(nested == 0 && fake.closures == 1);
    }

    // An event of that name already made by someone else: the channel is refused, not shared.
    {
        const DWORD pid = TestPid(4);
        wchar_t n[64];
        sc::EventName(n, L"stopped", pid);
        HANDLE squatter = CreateEventW(nullptr, TRUE, FALSE, n);
        CHECK(squatter);
        sc::HostChannel host;
        DWORD error = 0;
        CHECK(!host.Create(pid, &error) && error == ERROR_ALREADY_EXISTS && !host.Created());
        CloseHandle(squatter);
        CHECK(host.Create(pid) && host.Created());
    }

    // Core's closure refused on the launcher's stack: the wallet is still saved once, and the quit that follows
    // (Core's closure now runs, game.exit reaches the wallet again) repeats nothing.
    {
        const DWORD pid = TestPid(5);
        sc::HostChannel host;
        CHECK(host.Create(pid));
        Fake fake;
        fake.refuseClosure = true;
        std::thread launcher([&] { CHECK(sc::RequestStop(pid, 5000).outcome == sc::Outcome::Acknowledged); });
        for (unsigned polls = 0; !host.Poll([&] { fake.LauncherWork(); }); ++polls) { CHECK(polls < 5000); Sleep(1); }
        launcher.join();
        CHECK(fake.closures == 0 && fake.walletSaves == 1);
        CHECK(fake.Count("[app] stop requested by the launcher: exit work done except Core's closure "
                         "(refused on this stack; wallet saved directly)") == 1);
        fake.refuseClosure = false;
        fake.CoreStop();   // the real quit
        CHECK(fake.closures == 1 && fake.gameExit == 1 && fake.walletSaves == 1);
        CHECK(fake.Count("[contracts] wallet saved at exit") == 1);
    }

    // Retire: the mod's closure was reached some other way (WM_QUIT, lost admission). A later Stop is answered at
    // once instead of timing out, and the work does not run again.
    {
        const DWORD pid = TestPid(6);
        sc::HostChannel host;
        CHECK(host.Create(pid));
        Fake fake;
        host.Retire();
        const sc::Request r = sc::RequestStop(pid, 5000);
        CHECK(r.outcome == sc::Outcome::Acknowledged && r.elapsedMs < 1000);
        CHECK(!host.Poll([&] { fake.LauncherWork(); }) && fake.closures == 0);
    }

    // Retire from inside the launcher's own work (the closure retires the channel) must not answer early: the
    // launcher is released at the end of the work, not in the middle of it.
    {
        const DWORD pid = TestPid(7);
        sc::HostChannel host;
        CHECK(host.Create(pid));
        Fake fake;
        wchar_t n[64];
        sc::EventName(n, L"stopped", pid);
        HANDLE stopped = OpenEventW(SYNCHRONIZE, FALSE, n);
        CHECK(stopped);
        HANDLE stop = OpenEventW(EVENT_MODIFY_STATE, FALSE, (sc::EventName(n, L"stop", pid), n));
        CHECK(stop && SetEvent(stop));
        bool answeredInside = true;
        CHECK(host.Poll([&] {
            fake.LauncherWork();
            host.Retire();
            answeredInside = WaitForSingleObject(stopped, 0) == WAIT_OBJECT_0;
        }));
        CHECK(!answeredInside && WaitForSingleObject(stopped, 0) == WAIT_OBJECT_0 && fake.closures == 1);
        CloseHandle(stop);
        CloseHandle(stopped);
    }

    // The channel is made on another thread while the game thread is already polling: nothing is half made.
    {
        const DWORD pid = TestPid(8);
        sc::HostChannel host;
        std::atomic<bool> stopPolling{false};
        unsigned runs = 0;
        std::thread game([&] { while (!stopPolling) { host.Poll([&] { ++runs; }); Sleep(0); } });
        Sleep(20);
        CHECK(host.Create(pid));
        CHECK(sc::RequestStop(pid, 5000).outcome == sc::Outcome::Acknowledged);
        stopPolling = true;
        game.join();
        CHECK(runs == 1);
    }

    // Retired before the channel was created (the closure came early): Create answers at once and keeps it retired.
    {
        const DWORD pid = TestPid(9);
        sc::HostChannel host;
        Fake fake;
        host.Retire();
        CHECK(host.Create(pid));
        const sc::Request r = sc::RequestStop(pid, 5000);
        CHECK(r.outcome == sc::Outcome::Acknowledged && r.elapsedMs < 1000);
        CHECK(!host.Poll([&] { fake.LauncherWork(); }) && fake.closures == 0);   // still retired: no work
    }

    // Retire racing a Poll that has claimed the work: the stopped event is never set before the work has ended,
    // and it is set once the dust settles whichever side wins. (Stress: the interleavings differ run to run.)
    for (unsigned round = 0; round < 300; ++round) {
        const DWORD pid = TestPid(16 + (round % 8));
        sc::HostChannel host;
        CHECK(host.Create(pid));
        wchar_t n[64];
        sc::EventName(n, L"stopped", pid);
        HANDLE stopped = OpenEventW(SYNCHRONIZE, FALSE, n);
        HANDLE stop = OpenEventW(EVENT_MODIFY_STATE, FALSE, (sc::EventName(n, L"stop", pid), n));
        CHECK(stopped && stop);
        std::atomic<bool> started{false}, ended{false}, early{false}, go{false};
        std::thread observer([&] {
            while (!ended) {
                if (WaitForSingleObject(stopped, 0) == WAIT_OBJECT_0 && started && !ended) early = true;
            }
        });
        std::thread retirer([&] {
            while (!go) {}
            for (unsigned spin = round % 50; spin; --spin) {}
            host.Retire();
        });
        SetEvent(stop);
        go = true;
        host.Poll([&] {
            started = true;
            for (volatile unsigned i = 0; i < 20000; ++i) {}   // the exit work takes a moment
            ended = true;
        });
        if (!started) ended = true;   // retired first: no work, the observer may stop
        retirer.join();
        observer.join();
        CHECK(!early);
        CHECK(WaitForSingleObject(stopped, 1000) == WAIT_OBJECT_0);
        CloseHandle(stop);
        CloseHandle(stopped);
    }

    std::puts("stop channel: all checks passed");
    return 0;
}
