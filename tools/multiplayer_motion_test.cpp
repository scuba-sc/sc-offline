// SPDX-License-Identifier: GPL-3.0-only
// Ghost motion, measured. Two parts, synthetic public services only; no sockets, game or host.
//   A. The sample buffer on its own (plugins/multiplayer/motion.h): interpolation across lost poses, the
//      dejittered timeline, the capped extrapolation (also of a sender slower than 10 Hz, one that slows down
//      during a track, and across long gaps in its sequence), the smoothness of new 10 Hz tracks against the
//      motion.h before the slow-sender work, and the resets (stale, teleport, frame change).
//   B. The loaded modules on a jittered, lossy 10 Hz path: the same poses reach the unmodified #126 module
//      and the adapted one, in real time, and each module's ghost is followed frame by frame (60 per
//      second). The ghost of the adapted module must freeze less and jump less.
// Usage: multiplayer-motion-test <adapted module> [<unmodified #126 module>]
//        multiplayer-motion-test --track-tests        (part A only: the buffer, no module, a few milliseconds)
#include <sco_api.h>
#include <sco_net.h>
#include <sc_private_session.h>
#include <sc_spatial.h>
#include <sc_game_spatial.h>
#include <sc_actors.h>
#include <sc_entities.h>
#include "../plugins/multiplayer/multiplayer_wire.h"
#include "../plugins/multiplayer/motion.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <thread>
#include <vector>
#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {
unsigned passed = 0, failed = 0;
const char* testName = "";   // the test running, named in a FAIL line
#define CHECK(x) do { if (x) ++passed; else { ++failed; std::printf("FAIL %d: %s (%s)\n", __LINE__, #x, testName); } } while (0)
#define RUN(f) do { testName = #f; f(); } while (0)

using Bytes = std::vector<uint8_t>;

mpwire::Pose MakePose(uint32_t seq, double x, double y = 0, const char* zone = "Motion_Zone") {
    mpwire::Pose pose{};
    pose.seq = seq;
    pose.levels = 1;
    std::snprintf(pose.lv[0].name, sizeof(pose.lv[0].name), "%s", zone);
    pose.lv[0].pos[0] = x;
    pose.lv[0].pos[1] = y;
    pose.lv[0].rot[3] = 1;
    return pose;
}

// ---- A. the sample buffer ------------------------------------------------------------------------------

constexpr double kStep = 0.6;   // metres per 100 ms: 6 m/s

void TestInterpolationAndGaps() {
    mpmotion::Track track;
    // Poses 1..10, one every 100 ms from t = 1000, on a straight line; poses 5 and 6 are lost.
    for (uint32_t k = 1; k <= 10; ++k) {
        if (k == 5 || k == 6) continue;
        CHECK(track.Add(MakePose(k, kStep * k), 1000 + 100 * k) != mpmotion::Added::Dropped);
    }
    CHECK(track.Valid() && track.Count() == 8);
    double pos[3], rot[4];
    // Drawn exactly at pose 4's time: pose 4. Across the gap (4 to 7) the ghost keeps moving in a line.
    const double t4 = track.TimeOf(3);
    CHECK(std::fabs(t4 - 1400) < 1e-6);
    CHECK(track.Sample(1400 + 200, false, 0, pos, rot) && std::fabs(pos[0] - kStep * 4) < 1e-9);
    CHECK(track.Sample(1550 + 200, false, 0, pos, rot) && std::fabs(pos[0] - kStep * 5.5) < 1e-9);
    CHECK(track.Sample(1700 + 200, false, 0, pos, rot) && std::fabs(pos[0] - kStep * 7) < 1e-9);
    CHECK(std::fabs(rot[3] - 1) < 1e-12);
    // Drawn before the oldest sample: the oldest. Never a position that was not sent.
    CHECK(track.Sample(0, false, 0, pos, rot) && std::fabs(pos[0] - kStep * 1) < 1e-9);
}

void TestJitterIsRemoved() {
    mpmotion::Track track;
    std::mt19937_64 rng(7);
    std::uniform_real_distribution<double> jitter(-35, 35);
    double rawSum = 0, fitSum = 0, worstFit = 0;
    unsigned n = 0;
    for (uint32_t k = 1; k <= 120; ++k) {
        const uint64_t arrival = static_cast<uint64_t>(5000 + 100.0 * k + 60 + jitter(rng));
        track.Add(MakePose(k, kStep * k), arrival);
        if (k >= 20) {
            // The sample two places behind the newest is the one the ghost is drawn from.
            const unsigned i = track.Count() - 3;
            const double truthNow = 5000 + 100.0 * k + 60;
            const double truthThen = 5000 + 100.0 * (k - 2) + 60;
            rawSum += (static_cast<double>(arrival) - truthNow) * (static_cast<double>(arrival) - truthNow);
            fitSum += (track.TimeOf(i) - truthThen) * (track.TimeOf(i) - truthThen);
            worstFit = std::max(worstFit, std::fabs(track.TimeOf(i) - truthThen));
            ++n;
        }
    }
    std::printf("timeline: arrival error %.1f ms rms, fitted error %.1f ms rms (worst %.1f ms)\n", std::sqrt(rawSum / n), std::sqrt(fitSum / n), worstFit);
    CHECK(std::sqrt(fitSum / n) < 0.7 * std::sqrt(rawSum / n) && worstFit < 45);
    // The kept samples are strictly increasing in time.
    for (unsigned i = 1; i < track.Count(); ++i) CHECK(track.TimeOf(i) > track.TimeOf(i - 1));
    CHECK(track.Count() == mpmotion::kRing);
}

void TestExtrapolationIsCapped() {
    mpmotion::Track track;
    for (uint32_t k = 1; k <= 8; ++k) track.Add(MakePose(k, kStep * k), 1000 + 100 * k);
    const double last = track.TimeOf(track.Count() - 1);
    double pos[3], rot[4];
    // 50 ms past the newest sample: 50 ms of velocity. Far past it: 150 ms of velocity, then it holds.
    CHECK(track.Sample(static_cast<uint64_t>(last + 200 + 50), false, 0, pos, rot) && std::fabs(pos[0] - (kStep * 8 + 0.006 * 50)) < 1e-9);
    CHECK(track.Sample(static_cast<uint64_t>(last + 200 + 150), false, 0, pos, rot) && std::fabs(pos[0] - (kStep * 8 + 0.006 * 150)) < 1e-9);
    CHECK(track.Sample(static_cast<uint64_t>(last + 200 + 400), false, 0, pos, rot) && std::fabs(pos[0] - (kStep * 8 + 0.006 * 150)) < 1e-9);
    CHECK(track.Sample(static_cast<uint64_t>(last + 200 + 5000), false, 0, pos, rot) && std::fabs(pos[0] - (kStep * 8 + 0.006 * 150)) < 1e-9);
    // A single sample has no velocity: it holds.
    mpmotion::Track one;
    one.Add(MakePose(1, 3), 1000);
    CHECK(one.Sample(5000, false, 0, pos, rot) && pos[0] == 3);
}

void TestResets() {
    double pos[3], rot[4];
    // Stale: nothing for 700 ms. The next pose starts a track and the ghost is there at once.
    mpmotion::Track stale;
    for (uint32_t k = 1; k <= 6; ++k) stale.Add(MakePose(k, kStep * k), 1000 + 100 * k);
    CHECK(stale.Add(MakePose(7, 50), 1600 + 700) == mpmotion::Added::ResetStale);
    CHECK(stale.Count() == 1 && stale.Sample(1600 + 700, false, 0, pos, rot) && pos[0] == 50);
    // A sender that starts counting again (a lower sequence) is accepted after a silence, not dropped for ever.
    CHECK(stale.Add(MakePose(1, 51), 2300 + 900) == mpmotion::Added::ResetStale && stale.Count() == 1);

    // Teleport: 5 km in 100 ms resets; 200 m in 100 ms (2 km/s) is fast motion and does not.
    mpmotion::Track jump;
    for (uint32_t k = 1; k <= 5; ++k) jump.Add(MakePose(k, kStep * k), 1000 + 100 * k);
    CHECK(jump.Add(MakePose(6, 200), 1600) == mpmotion::Added::Appended);
    CHECK(jump.Add(MakePose(7, 5200), 1700) == mpmotion::Added::ResetTeleport && jump.Count() == 1);
    CHECK(jump.Sample(1700, false, 0, pos, rot) && pos[0] == 5200);

    // Frame change: another zone, or a ship boarded.
    mpmotion::Track frame;
    for (uint32_t k = 1; k <= 5; ++k) frame.Add(MakePose(k, kStep * k), 1000 + 100 * k);
    CHECK(frame.Add(MakePose(6, 3.6, 0, "Other_Zone"), 1600) == mpmotion::Added::ResetFrame && frame.Count() == 1);
    // The same zone with a parent above it is another chain of frames; so is boarding a ship (the ship section appears).
    const auto two = [](uint32_t seq, bool ship) {
        mpwire::Pose pose = MakePose(seq, 0.6 * seq);
        pose.levels = 2;
        std::snprintf(pose.lv[1].name, sizeof(pose.lv[1].name), "Parent_Zone");
        pose.lv[1].rot[3] = 1;
        if (ship) { pose.ship = true; pose.shipFirst = 1; pose.shipRot[1][3] = 1; }
        return pose;
    };
    mpmotion::Track chain;
    for (uint32_t k = 1; k <= 4; ++k) chain.Add(MakePose(k, kStep * k), 1000 + 100 * k);
    CHECK(chain.Add(two(5, false), 1500) == mpmotion::Added::ResetFrame && chain.Count() == 1);
    CHECK(chain.Add(two(6, false), 1600) == mpmotion::Added::Appended && chain.Count() == 2);
    CHECK(chain.Add(two(7, true), 1700) == mpmotion::Added::ResetFrame && chain.Count() == 1);
    CHECK(chain.Add(two(8, true), 1800) == mpmotion::Added::Appended && chain.Sample(1800 + 200, true, 1, pos, rot));
}

void TestOrder() {
    mpmotion::Track track;
    CHECK(track.Add(MakePose(10, 1), 1000) == mpmotion::Added::Appended);
    CHECK(track.Add(MakePose(10, 1), 1100) == mpmotion::Added::Dropped);   // a repeat
    CHECK(track.Add(MakePose(9, 1), 1100) == mpmotion::Added::Dropped);    // older
    CHECK(track.Add(MakePose(11, 1.6), 1100) == mpmotion::Added::Appended);
    // The sequence wraps at 2^32; the track goes on across it, and a pose from before the wrap is older.
    mpmotion::Track wrap;
    uint32_t seq = 0xFFFFFFFDu;
    for (int i = 0; i < 6; ++i, ++seq) CHECK(wrap.Add(MakePose(seq, 0.6 * i), 1000 + 100 * static_cast<uint64_t>(i)) == mpmotion::Added::Appended);
    CHECK(wrap.Count() == 6 && wrap.TimeOf(5) > wrap.TimeOf(2));
    CHECK(wrap.Add(MakePose(0xFFFFFFFEu, 0), 1600) == mpmotion::Added::Dropped);
}

// ---- A2. extrapolation is capped in distance, whatever the arrival times are ---------------------------------
// The ghost goes on past the newest pose for at most kMaxExtrapolateMs at the real speed. The real speed is the
// sender's (one pose per 100 ms at least), never the speed the arrival times suggest: poses delivered in the same
// frame (a burst after a stall, the first two after a reset) arrive 1 ms apart. Each case feeds deliveries to a
// track in lockstep with a 5 ms frame clock, as the module's tick does, reads the ghost at every frame and measures
// its lead over the newest pose added so far.

struct Delivery {
    uint64_t at;            // when the network delivers it; it is added at the first frame at or after
    uint32_t seq;
    const char* zone;
};

struct LeadResult {
    double worst = -1e9;        // the largest lead over the newest pose at any frame
    double inSilence = -1e9;    // the same, at the frames after the last delivery (the extrapolation)
    unsigned resets = 0;        // teleport resets
};

// xOf(seq): where the sender was when it sent pose `seq`, in metres along x.
LeadResult LeadAlong(const std::function<double(uint32_t)>& xOf, std::vector<Delivery> deliveries, uint64_t until) {
    std::stable_sort(deliveries.begin(), deliveries.end(), [](const Delivery& a, const Delivery& b) { return a.at < b.at; });
    mpmotion::Track track;
    LeadResult r;
    size_t next = 0;
    double newestX = 0;
    const uint64_t lastAt = deliveries.back().at;
    for (uint64_t now = deliveries.front().at - deliveries.front().at % 5; now <= until; now += 5) {
        while (next < deliveries.size() && deliveries[next].at <= now) {
            const Delivery& d = deliveries[next++];
            const mpmotion::Added added = track.Add(MakePose(d.seq, xOf(d.seq), 0, d.zone), now);
            if (added != mpmotion::Added::Dropped) newestX = xOf(d.seq);
            if (added == mpmotion::Added::ResetTeleport) ++r.resets;
        }
        double pos[3], rot[4];
        if (!track.Sample(now, false, 0, pos, rot)) continue;
        const double lead = pos[0] - newestX;
        r.worst = std::max(r.worst, lead);
        if (now >= lastAt + 200) r.inSilence = std::max(r.inSilence, lead);
    }
    return r;
}

// A sender at a constant cadence: one pose per cadenceMs, `speed` metres a second.
LeadResult LeadOf(double speed, std::vector<Delivery> deliveries, uint64_t until, uint64_t cadenceMs = 100) {
    const double step = speed * static_cast<double>(cadenceMs) / 1000.0;   // metres per sequence step
    return LeadAlong([step](uint32_t seq) { return step * seq; }, std::move(deliveries), until);
}

std::vector<Delivery> Steady(uint32_t from, uint32_t to, uint64_t base, const char* zone = "Motion_Zone", uint64_t cadenceMs = 100) {
    std::vector<Delivery> out;
    for (uint32_t k = from; k <= to; ++k) out.push_back({ base + cadenceMs * k, k, zone });
    return out;
}

void Append(std::vector<Delivery>& to, const std::vector<Delivery>& more) { to.insert(to.end(), more.begin(), more.end()); }

void ExpectLead(const char* what, double speed, const LeadResult& r, bool extrapolates, double tolerance = 1.02) {
    const double cap = speed * mpmotion::kMaxExtrapolateMs / 1000.0;   // the most the ghost may lead the newest pose by
    std::printf("lead %-52s %6.0f m/s: worst %7.2f m, in the silence %7.2f m, cap %6.2f m\n", what, speed, r.worst, r.inSilence, cap);
    CHECK(r.worst <= cap * tolerance + 1e-9);
    if (extrapolates) CHECK(r.inSilence >= cap * 0.5);   // and it is the extrapolation that was measured, not a ghost that stood still
    CHECK(r.resets == 0);
}

// The same for a track too young to know the sender's cadence: once the last pose is in, the ghost stands exactly at the
// newest pose and does not go ahead of it. (The frames before it may belong to an older, steady track.)
void ExpectHold(const char* what, double speed, const LeadResult& r) {
    ExpectLead(what, speed, r, false);
    CHECK(std::fabs(r.inSilence) <= 1e-9);
}

void TestExtrapolationLeadIsCapped() {
    for (const double speed : { 6.0, 200.0, 2500.0 }) {
        // (a) Two poses delivered in the same frame, then nothing, in steady flight: the poses 21 and 22 were held up
        //     for 300 ms and arrive together, and the following poses are lost.
        {
            std::vector<Delivery> d = Steady(1, 20, 1000);
            d.push_back({ 3400, 21, "Motion_Zone" });
            d.push_back({ 3400, 22, "Motion_Zone" });
            ExpectLead("two poses in one frame, steady flight", speed, LeadOf(speed, d, 3400 + 3000), true);
        }
        // (a) The first two poses after a reset arrive in one frame, then nothing. The track is new and cannot know the
        //     sender's cadence (two poses delivered together do not say it), so the ghost holds at the newest pose.
        {
            std::vector<Delivery> d = { { 5000, 1, "Motion_Zone" }, { 5000, 2, "Motion_Zone" } };
            ExpectHold("two poses in one frame, after a reset", speed, LeadOf(speed, d, 5000 + 3000));
        }
        // (a) The same after a change of frames (the player boards a ship, or enters another zone), which resets the track.
        {
            std::vector<Delivery> d = Steady(1, 10, 1000);
            d.push_back({ 2400, 11, "Other_Zone" });
            d.push_back({ 2400, 12, "Other_Zone" });
            ExpectHold("two poses in one frame, after a change of frames", speed, LeadOf(speed, d, 2400 + 3000));
        }
        // (b) A burst after a stall of 150 to 200 ms: the poses held up arrive together; the following ones are lost.
        for (const uint64_t stallEnd : { 3250, 3300, 3350, 3400 }) {
            std::vector<Delivery> d = Steady(1, 20, 1000);
            for (uint32_t k = 21; k <= 24; ++k) d.push_back({ stallEnd, k, "Motion_Zone" });
            char what[80];
            std::snprintf(what, sizeof(what), "burst of 4 after a stall ending at %llu", static_cast<unsigned long long>(stallEnd));
            ExpectLead(what, speed, LeadOf(speed, d, stallEnd + 3000), true);
        }
        // (b) The same burst, and the flow resumes: no frame of the whole run may lead by more than the cap.
        {
            std::vector<Delivery> d = Steady(1, 20, 1000);
            for (uint32_t k = 21; k <= 23; ++k) d.push_back({ 3350, k, "Motion_Zone" });
            Append(d, Steady(24, 60, 1000));
            ExpectLead("burst of 3 after a stall, flow resumes", speed, LeadOf(speed, d, 1000 + 100 * 60 + 500), false);
        }
    }
}

// A long run with jitter, loss and four stalls of 150 to 250 ms after which everything held up is delivered at once.
// No frame may lead by more than the cap, and no burst may look like a teleport.
void TestStallsAndBursts() {
    for (const double speed : { 6.0, 200.0, 2500.0 }) {
        std::mt19937_64 rng(2026);
        std::uniform_real_distribution<double> jitter(-35, 35), chance(0, 1);
        const uint64_t stallStart[] = { 4000, 9000, 15000, 22000 }, stallLength[] = { 150, 200, 250, 180 };
        std::vector<Delivery> d;
        for (uint32_t k = 1; k <= 300; ++k) {
            if (chance(rng) < 0.12) continue;
            uint64_t at = static_cast<uint64_t>(100.0 * k + 60 + jitter(rng));
            for (int i = 0; i < 4; ++i)
                if (at >= stallStart[i] && at < stallStart[i] + stallLength[i]) at = stallStart[i] + stallLength[i];
            d.push_back({ at, k, "Motion_Zone" });
        }
        const LeadResult r = LeadOf(speed, d, 30500);
        ExpectLead("jitter, loss and four stalls", speed, r, false);
    }
}

// ---- A3. senders slower than 10 Hz, rate changes, long gaps, new tracks ------------------------------------------
// The wire carries no sender time, so the track judges how long the sender takes per pose from the arrival times it has seen
// (motion.h, "Capped extrapolation"). Three kinds of case follow. Exact cases (no jitter) must stay under the cap, the most the
// ghost may lead the newest pose by: 150 ms at the real speed. Cases on the test's own network (60 ms +-35 ms, 12 in 100 lost)
// cannot be exact whatever is done, since two arrival times say little about a step: they are counted over many runs, and the
// worst measured lead is pinned (the number is in plugins/multiplayer/README.md, "Ghost motion"). And new 10 Hz tracks may not
// stutter more than they did before this work.

// A sender slower than 10 Hz: its game runs under 10 frames a second, so it sends a pose per frame, 200 ms or more apart. The
// 100 ms of the tick gate is the least it takes, not what it takes: two of its poses delivered in one frame, read at 100 ms
// per step, show twice the speed or more. Speeds stop at 1 km/s so that one step of a 250 ms sender (250 m) is still no teleport
// on the 100 ms floor (300 m): what is measured is the lead, not the reset.
void TestSlowSenderLeadIsCapped() {
    for (const double speed : { 6.0, 200.0, 1000.0 }) {
        for (const uint64_t cadence : { 200, 250 }) {
            char what[80];
            // (a) The first two poses of a track arrive in one frame, then nothing: the track cannot know the cadence, it holds.
            {
                std::vector<Delivery> d = { { 5000, 1, "Motion_Zone" }, { 5000, 2, "Motion_Zone" } };
                std::snprintf(what, sizeof(what), "%llu ms sender, two poses in one frame, new track", static_cast<unsigned long long>(cadence));
                ExpectHold(what, speed, LeadOf(speed, d, 5000 + 3000, cadence));
            }
            // (b) In steady flight the track knows the cadence: poses 21 and 22, held up, arrive together and the ghost goes on
            //     at the real speed for 150 ms.
            {
                std::vector<Delivery> d = Steady(1, 20, 1000, "Motion_Zone", cadence);
                const uint64_t at = 1000 + cadence * 22;
                d.push_back({ at, 21, "Motion_Zone" });
                d.push_back({ at, 22, "Motion_Zone" });
                std::snprintf(what, sizeof(what), "%llu ms sender, two poses in one frame, steady", static_cast<unsigned long long>(cadence));
                ExpectLead(what, speed, LeadOf(speed, d, at + 3000, cadence), true);
            }
            // (c) A burst of four on the same steady track, the following poses lost. The first of the four arrives as late as the
            //     stale limit allows (600 ms after pose 20): later, and the track would start over and this would measure a new one.
            {
                std::vector<Delivery> d = Steady(1, 20, 1000, "Motion_Zone", cadence);
                const uint64_t at = 1000 + cadence * 20 + mpmotion::kStaleMs;
                for (uint32_t k = 21; k <= 24; ++k) d.push_back({ at, k, "Motion_Zone" });
                std::snprintf(what, sizeof(what), "%llu ms sender, burst of 4 after a stall", static_cast<unsigned long long>(cadence));
                // The fit is pulled down a little by the burst itself, so a 250 ms sender is led up to 2% over (measured 1.022).
                ExpectLead(what, speed, LeadOf(speed, d, at + 3000, cadence), true, 1.03);
            }
        }
    }
}

// The poses of a sender as the test's network delivers them: pose k was sent at sendMs[k] (index 0 unused) and arrives 60 ms
// later, +-jitterMs; with `lossy`, 12 in 100 are lost; the poses in `lost` always are.
std::vector<Delivery> Network(const std::vector<double>& sendMs, std::mt19937_64& rng, double jitterMs, bool lossy, const std::vector<uint32_t>& lost = {}) {
    std::uniform_real_distribution<double> jitter(-35, 35), chance(0, 1);
    std::vector<Delivery> out;
    for (uint32_t k = 1; k < sendMs.size(); ++k) {
        const double j = jitterMs > 0 ? jitter(rng) * (jitterMs / 35.0) : 0.0;
        const bool dropped = lossy && chance(rng) < 0.12;
        if (dropped || std::find(lost.begin(), lost.end(), k) != lost.end()) continue;
        out.push_back({ static_cast<uint64_t>(sendMs[k] + 60 + j), k, "Motion_Zone" });
    }
    return out;
}

// Pose k sent at the time sendMs[k]; the sender moves at `speed` m/s.
LeadResult LeadSent(double speed, const std::vector<double>& sendMs, const std::vector<Delivery>& d) {
    uint64_t last = 0;
    for (const Delivery& x : d) last = std::max(last, x.at);
    return LeadAlong([&](uint32_t seq) { return speed * sendMs[seq] / 1000.0; }, d, last + 2500);
}

std::vector<double> ConstantSender(unsigned n, double cadenceMs) {
    std::vector<double> sent(n + 1, 0.0);
    for (unsigned k = 1; k <= n; ++k) sent[k] = cadenceMs * k;
    return sent;
}

// 10 Hz for the first `fast` poses, then one pose every `slowMs`.
std::vector<double> RateDrop(unsigned n, unsigned fast, double slowMs) {
    std::vector<double> sent(n + 1, 0.0);
    for (unsigned k = 1; k <= n; ++k) sent[k] = sent[k - 1] + (k <= fast ? 100.0 : slowMs);
    return sent;
}

struct Runs { unsigned runs = 0, over = 0; double worst = 0; };

// Many runs on the test's network: the largest lead over the cap at the real speed, as a multiple of the cap.
Runs ManyRuns(double speed, const std::vector<double>& sent, unsigned runs) {
    Runs r;
    for (unsigned seed = 0; seed < runs; ++seed) {
        std::mt19937_64 rng(seed);
        const LeadResult l = LeadSent(speed, sent, Network(sent, rng, 35, true));
        const double ratio = l.worst / (speed * mpmotion::kMaxExtrapolateMs / 1000.0);
        r.worst = std::max(r.worst, ratio);
        if (ratio > 1.02) ++r.over;
        ++r.runs;
    }
    return r;
}

// Bounds measured on this test (the same fixed seeds every run), rounded up to the next 0.05. They are not promises of the
// design: they are what the numbers were when this was written, and a change that makes them worse must say so. (A hundred
// runs missed the tail: the worst of the first hundred seeds was 1.33, 1.25, 1.46, 1.43 and 1.32.)
constexpr unsigned kNetworkRuns = 1000;      // runs of each case, seeds 0 to 999
constexpr double kBoundConstant200 = 1.55;   // 200 ms sender (measured 1.54)
constexpr double kBoundConstant250 = 1.40;   // 250 ms sender (measured 1.39)
constexpr double kBoundDrop150 = 1.50;       // 10 Hz for 30 poses, then 150 / 200 / 250 ms a pose (measured 1.47, 1.72, 1.35)
constexpr double kBoundDrop200 = 1.75;
constexpr double kBoundDrop250 = 1.40;

// A constant slow sender on the test's own network, many runs.
void TestSlowSenderOnTheNetwork() {
    const struct { double cadence, bound; } cases[] = { { 200, kBoundConstant200 }, { 250, kBoundConstant250 } };
    for (const auto& c : cases) {
        const Runs r = ManyRuns(200.0, ConstantSender(60, c.cadence), kNetworkRuns);
        std::printf("net  %3.0f ms sender, 60 ms +-35 ms, 12%% lost: %u runs, %u over the 30 m cap, worst %.2f x the cap (pinned %.2f)\n",
                    c.cadence, r.runs, r.over, r.worst, c.bound);
        CHECK(r.worst <= c.bound);
    }
}

// A sender whose game drops under 10 frames a second DURING a track: the timeline fit has 10 Hz in it and follows only slowly.
void TestRateDropLeadIsCapped() {
    const double speed = 200.0;   // 30 m cap
    const struct { double slow, bound; } cases[] = { { 150, kBoundDrop150 }, { 200, kBoundDrop200 }, { 250, kBoundDrop250 } };
    for (const auto& c : cases) {
        const std::vector<double> sent = RateDrop(70, 30, c.slow);
        char what[80];
        std::mt19937_64 quiet(1);
        std::snprintf(what, sizeof(what), "10 Hz -> %.0f ms, exact network", c.slow);
        ExpectLead(what, speed, LeadSent(speed, sent, Network(sent, quiet, 0, false)), true);
        std::snprintf(what, sizeof(what), "10 Hz -> %.0f ms, exact network, pose 33 lost", c.slow);
        ExpectLead(what, speed, LeadSent(speed, sent, Network(sent, quiet, 0, false, { 33 })), true);
        const Runs r = ManyRuns(speed, sent, kNetworkRuns);
        std::printf("net  10 Hz -> %3.0f ms, 60 ms +-35 ms, 12%% lost: %u runs, %u over the 30 m cap, worst %.2f x the cap (pinned %.2f)\n",
                    c.slow, r.runs, r.over, r.worst, c.bound);
        CHECK(r.worst <= c.bound);
    }
}

// Arrival patterns a reviewer built to defeat a fit through 8 poses (200 ms sender, 200 m/s, cap 30 m).
void TestBunchedAndChangingArrivals() {
    const double speed = 200.0;
    const auto at = [](std::initializer_list<uint64_t> times, std::initializer_list<uint32_t> seqs) {
        std::vector<Delivery> d;
        auto t = times.begin();
        for (const uint32_t q : seqs) d.push_back({ *t++, q, "Motion_Zone" });
        return d;
    };
    const auto forty = [](uint32_t seq) { return 40.0 * seq; };   // a pose every 200 ms at 200 m/s
    // three bunched poses at the start, then a slow ramp
    ExpectLead("bunched start (1700 x3, 1800, 2000, ...)", speed, LeadAlong(forty, at({ 1700, 1700, 1700, 1800, 2000, 2200, 2600, 2600 }, { 1, 2, 3, 4, 5, 6, 7, 8 }), 2600 + 3000), false);
    // jitter +35 ms on the first four, -35 ms on the next four
    ExpectLead("jitter +35 x4 then -35 x4", speed, LeadAlong(forty, at({ 1235, 1435, 1635, 1835, 1965, 2165, 2365, 2565 }, { 1, 2, 3, 4, 5, 6, 7, 8 }), 2565 + 3000), false);
    // the same with poses 7 lost
    ExpectLead("bunched start, pose 7 lost", speed, LeadAlong(forty, at({ 1700, 1700, 1700, 1800, 2000, 2200, 2800, 2800 }, { 1, 2, 3, 4, 5, 6, 8, 9 }), 2800 + 3200), false);
    // 10 Hz for 20 poses, then 200 ms a pose: 20 m a pose, then 40 m a pose
    {
        std::vector<Delivery> d;
        for (uint32_t k = 1; k <= 20; ++k) d.push_back({ 1000 + 100 * k, k, "Motion_Zone" });
        d.push_back({ 3200, 21, "Motion_Zone" });
        d.push_back({ 3400, 22, "Motion_Zone" });
        const auto path = [](uint32_t seq) { return seq <= 20 ? 20.0 * seq : 40.0 * seq - 400.0; };
        ExpectLead("10 Hz for 20 poses, then 200 ms a pose", speed, LeadAlong(path, d, 3400 + 3000), false);
    }
}

// New 10 Hz tracks (every join, silence, change of frames and teleport starts one) may not stutter more than they did before
// the slow-sender work: 2000 new tracks on the test's network (60 ms +-35 ms, 12 in 100 lost, pose 1 not), 6.4 m/s, 60 frames a
// second, the first 1.5 s of each, counted by the rules of part B (a freeze: 4 frames in a row moving under a quarter of the
// player; a jump: a frame moving more than three times the player). The frames of the 200 ms playout wait are left out.
struct Stutter { unsigned freezes = 0, jumps = 0, tracksWith = 0; double largest = 0; };

Stutter YoungTracks(unsigned tracks) {
    std::mt19937_64 rng(2026);
    std::uniform_real_distribution<double> jitter(-35, 35), chance(0, 1);
    const double expected = 6.4 / 60.0;
    Stutter total;
    for (unsigned n = 0; n < tracks; ++n) {
        std::vector<std::pair<uint64_t, uint32_t>> arrivals;
        for (uint32_t k = 1; k < 40; ++k) {
            if (k > 1 && chance(rng) < 0.12) continue;
            arrivals.push_back({ static_cast<uint64_t>(100.0 * k + 60 + jitter(rng)), k });
        }
        std::sort(arrivals.begin(), arrivals.end());
        mpmotion::Track track;
        size_t next = 0;
        std::vector<double> frames;
        for (double now = static_cast<double>(arrivals.front().first); now < static_cast<double>(arrivals.front().first) + 1500; now += 1000.0 / 60) {
            const uint64_t ms = static_cast<uint64_t>(now);
            while (next < arrivals.size() && static_cast<double>(arrivals[next].first) <= now) {
                track.Add(MakePose(arrivals[next].second, 0.64 * arrivals[next].second), ms);
                ++next;
            }
            double pos[3], rot[4];
            track.Sample(ms, false, 0, pos, rot);
            frames.push_back(pos[0]);
        }
        unsigned freezes = 0, jumps = 0, run = 0;
        for (size_t i = 14; i < frames.size(); ++i) {
            const double d = std::fabs(frames[i] - frames[i - 1]);
            total.largest = std::max(total.largest, d);
            if (d < 0.25 * expected) ++run; else { if (run >= 4) ++freezes; run = 0; }
            if (d > 3 * expected) ++jumps;
        }
        if (run >= 4) ++freezes;
        total.freezes += freezes;
        total.jumps += jumps;
        if (freezes + jumps) ++total.tracksWith;
    }
    return total;
}

// What the motion.h of f2ba0a0 (before the slow-sender work) gives on the same inputs: this test run against that motion.h
// prints exactly these numbers. A change may match them or do better, never worse.
constexpr unsigned kYoungTracks = 2000;
constexpr unsigned kBeforeFreezes = 41;
constexpr unsigned kBeforeJumps = 61;
constexpr unsigned kBeforeTracksWith = 61;

void TestYoungTracksSmooth() {
    const Stutter s = YoungTracks(kYoungTracks);
    std::printf("young %u new 10 Hz tracks, first 1.5 s: freezes %u, jumps %u, tracks with a stutter %u, largest step %.2f m (before the slow-sender work: %u, %u, %u)\n",
                kYoungTracks, s.freezes, s.jumps, s.tracksWith, s.largest, kBeforeFreezes, kBeforeJumps, kBeforeTracksWith);
    CHECK(s.freezes <= kBeforeFreezes);
    CHECK(s.jumps <= kBeforeJumps);
    CHECK(s.tracksWith <= kBeforeTracksWith);
}

// A long gap in the sequence delivered in one frame into a new track: 15 poses lost and the 16th arrives with the first (a 100 ms
// sender). Nothing says how long the sender took for 16 steps, so the ghost holds; and a gap the track cannot read is no teleport.
void TestLongGaps() {
    for (const double speed : { 6.0, 200.0, 2500.0 }) {
        std::vector<Delivery> d = { { 1000, 1, "Motion_Zone" }, { 1000, 17, "Motion_Zone" } };
        ExpectHold("gap of 16 steps in one frame, new track", speed, LeadOf(speed, d, 1000 + 3000));
        d = { { 1000, 1, "Motion_Zone" }, { 1000, 11, "Motion_Zone" } };
        ExpectHold("gap of 10 steps in one frame, new track", speed, LeadOf(speed, d, 1000 + 3000));
    }
    mpmotion::Track fast;
    CHECK(fast.Add(MakePose(1, 0), 1000) == mpmotion::Added::Appended);
    CHECK(fast.Add(MakePose(11, 2500), 1000) == mpmotion::Added::Appended && fast.Count() == 2);   // 2.5 km/s for 10 steps: motion
    // 3 km/s stays the limit across a long gap: 10 steps allow 3000 m and no more.
    mpmotion::Track over;
    CHECK(over.Add(MakePose(1, 0), 1000) == mpmotion::Added::Appended);
    CHECK(over.Add(MakePose(11, 3001), 1000) == mpmotion::Added::ResetTeleport && over.Count() == 1);
    // A very long gap does not switch the test off: the steps count up to kMaxTeleportSteps (50 steps, 15 km).
    mpmotion::Track longGap;
    CHECK(longGap.Add(MakePose(1, 0), 1000) == mpmotion::Added::Appended);
    CHECK(longGap.Add(MakePose(101, 14000), 1000) == mpmotion::Added::Appended);
    mpmotion::Track tooFar;
    CHECK(tooFar.Add(MakePose(1, 0), 1000) == mpmotion::Added::Appended);
    CHECK(tooFar.Add(MakePose(101, 16000), 1000) == mpmotion::Added::ResetTeleport);
}

// The sequence steps of a gap count as they are, with no hold in the way: a track that knows its cadence (20 steady poses),
// then pose 21 and pose 37 (a gap of 16 steps) delivered in one frame. The speed is the distance over 16 steps (1600 ms), so the
// lead is the cap; read as 8 steps it would be twice the cap. The same at 2.5 km/s, where 16 steps are 4000 m, no teleport.
void TestLongGapSpeedUsesEverySequenceStep() {
    for (const double speed : { 6.0, 200.0, 2500.0 }) {
        std::vector<Delivery> d = Steady(1, 20, 1000);
        d.push_back({ 3400, 21, "Motion_Zone" });
        d.push_back({ 3400, 37, "Motion_Zone" });
        ExpectLead("gap of 16 steps in one frame, known cadence", speed, LeadOf(speed, d, 3400 + 3000), true);
    }
}

// The teleport limit follows the known cadence: 2.5 km/s at a pose every 200 ms is 500 m a step, which the 100 ms floor (300 m)
// would call a teleport when two poses are delivered together. A real jump still resets.
void TestTeleportFollowsTheCadence() {
    const auto steady = [](mpmotion::Track& t) {
        for (uint32_t k = 1; k <= 20; ++k) CHECK(t.Add(MakePose(k, 500.0 * k), 1000 + 200 * k) == mpmotion::Added::Appended);
    };
    mpmotion::Track together;
    steady(together);
    CHECK(together.Add(MakePose(21, 500.0 * 21), 5400) == mpmotion::Added::Appended);
    CHECK(together.Add(MakePose(22, 500.0 * 22), 5400) == mpmotion::Added::Appended);
    mpmotion::Track jump;
    steady(jump);
    CHECK(jump.Add(MakePose(21, 500.0 * 20 + 700), 5200) == mpmotion::Added::ResetTeleport);   // 700 m in 200 ms is over 3 km/s
    mpmotion::Track distant;   // not "far": windows.h defines that word
    steady(distant);
    CHECK(distant.Add(MakePose(21, 500.0 * 20 + 5000), 5200) == mpmotion::Added::ResetTeleport);
    // A slower cadence does not widen the limit without bound: a pose every 400 ms allows 1200 m a step, no more.
    mpmotion::Track slow;
    for (uint32_t k = 1; k <= 20; ++k) CHECK(slow.Add(MakePose(k, 1000.0 * k), 1000 + 400 * k) == mpmotion::Added::Appended);
    CHECK(slow.Add(MakePose(21, 1000.0 * 20 + 1300), 9400) == mpmotion::Added::ResetTeleport);
}

// Poses delivered in one frame are not a teleport: the sender took 100 ms for each step whatever the arrival times say.
void TestBurstIsNotATeleport() {
    mpmotion::Track track;
    const double step = 250.0;   // 2.5 km/s: fast, but below the teleport speed (3 km/s)
    for (uint32_t k = 1; k <= 10; ++k) CHECK(track.Add(MakePose(k, step * k), 1000 + 100 * k) == mpmotion::Added::Appended);
    for (uint32_t k = 11; k <= 14; ++k) CHECK(track.Add(MakePose(k, step * k), 2400) == mpmotion::Added::Appended);
    // A real teleport still is one, delivered together or not: 5 km in one step.
    CHECK(track.Add(MakePose(15, step * 14 + 5000), 2400) == mpmotion::Added::ResetTeleport);
    CHECK(track.Add(MakePose(16, step * 14 + 5000 + step), 2500) == mpmotion::Added::Appended);
    CHECK(track.Count() == 2);
    // After a teleport reset the sequence restarts with the pose that reset the track: the next one is one step on, so the
    // ghost leads by exactly 1.5 steps (150 ms at the speed of one step per 100 ms), not by a count mixed up with the old track's.
    double pos[3], rot[4];
    const double newestX = step * 14 + 5000 + step;
    CHECK(track.Sample(2500 + 400, false, 0, pos, rot) && std::fabs(pos[0] - (newestX + 1.5 * step)) < 1e-6);
}

// ---- B. the loaded modules -----------------------------------------------------------------------------

using Clock = std::chrono::steady_clock;
Clock::time_point t0;
double SimMs() { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); }

struct Frame { double ms; double pos[3]; };

struct Module {
    std::string label;
    sco_plugin* owner = nullptr;
    void* handle = nullptr;
    sco_plugin_query_fn query = nullptr;
    sco_plugin_load_fn load = nullptr;
    sco_plugin_unload_fn unload = nullptr;
    std::map<std::string, sco_net_on_message> messages;
    std::map<std::string, sco_event_fn> events;
    uint64_t ghost = 0;
    std::vector<Frame> frames;
    unsigned sends = 0;
};
std::vector<Module*> modules;
uint64_t nextEntity = 100;

Module* Of(sco_plugin* s) {
    for (Module* m : modules) if (m->owner == s) return m;
    return nullptr;
}

sco_result Register(sco_plugin*, const sco_command*) { return SCO_OK; }
sco_result Subscribe(sco_plugin* s, const char* event, sco_event_fn fn, void*) { Module* m = Of(s); if (!m) return SCO_BAD_ARG; m->events[event] = fn; return SCO_OK; }
sco_result Unsubscribe(sco_plugin* s, const char* event, sco_event_fn) { Module* m = Of(s); if (!m) return SCO_BAD_ARG; m->events.erase(event); return SCO_OK; }
void LogFn(sco_plugin*, sco_log_level, const char*) {}
int NetActive() { return 1; }
uint64_t Self() { return 3; }
sco_result GetPeers(sco_net_peer* out, uint32_t* count) {
    if (*count < 2) return SCO_TOO_MANY;
    out[0] = { 3, 0 };
    out[1] = { 2, 0 };
    *count = 2;
    return SCO_OK;
}
sco_result PeerName(uint64_t, char* out, uint32_t cap) { std::snprintf(out, cap, "Walker"); return SCO_OK; }
sco_result Send(sco_plugin* s, const char*, const void*, uint32_t) { if (Module* m = Of(s)) ++m->sends; return SCO_OK; }
sco_result RegisterChannel(sco_plugin* s, const char* id, uint32_t, uint32_t, sco_net_on_message cb, void*) {
    Module* m = Of(s);
    if (!m) return SCO_BAD_ARG;
    m->messages[id] = cb;
    return SCO_OK;
}
sco_result UnregisterChannel(sco_plugin* s, const char* id) { if (Module* m = Of(s)) m->messages.erase(id); return SCO_OK; }
sco_result BrokerState(sc_private_session_state_v1* out) { out->state = SC_PRIVATE_SESSION_CONNECTED; out->peer_count = 2; return SCO_OK; }
sco_result BrokerLeave(sco_plugin*) { return SCO_OK; }
int PlayerPose(double p[3], double q[4], uint64_t* zone) { p[0] = p[1] = p[2] = 0; q[0] = q[1] = q[2] = 0; q[3] = 1; *zone = 7; return 1; }
int ZoneRecord(uint64_t id, sc_game_zone* out) {
    if (id != 7) return 0;
    out->epoch = 9; out->zone_id = 7; out->root_id = 7; out->parent_id = 0;
    std::snprintf(out->name, sizeof(out->name), "Motion_Zone");
    return 1;
}
int FindZone(const char* name, sc_game_zone* out) { return !std::strcmp(name, "Motion_Zone") ? ZoneRecord(7, out) : 0; }
int Convert(uint64_t, uint64_t, const double p[3], const double q[4], double op[3], double oq[4]) {
    std::memcpy(op, p, 3 * sizeof(double));
    std::memcpy(oq, q, 4 * sizeof(double));
    return 1;
}
sco_result SpawnActor(sco_plugin* s, const char*, uint64_t, const double[3], uint64_t* id) {
    Module* m = Of(s);
    if (!m) return SCO_BAD_ARG;
    *id = m->ghost = nextEntity++;
    return SCO_OK;
}
sco_result DespawnActor(sco_plugin*, uint64_t) { return SCO_OK; }
int Alive(uint64_t) { return 1; }
sco_result SetTransform(sco_plugin* s, uint64_t id, uint64_t, const double p[3], const double[4]) {
    Module* m = Of(s);
    if (m && id == m->ghost) m->frames.push_back({ SimMs(), { p[0], p[1], p[2] } });
    return SCO_OK;
}
sco_net_v1 net{};
sc_private_session_v1 broker{};
sc_spatial_v1 spatial{};
sc_game_spatial_v1 poses{};
sc_actors_v1 actors{};
sc_entities_v1 entities{};
sco_result Query(const char* name, uint32_t, const void** out) {
    *out = nullptr;
    if (!std::strcmp(name, SCO_NET_NAME)) *out = &net;
    else if (!std::strcmp(name, SC_PRIVATE_SESSION_NAME)) *out = &broker;
    else if (!std::strcmp(name, SC_SPATIAL_SERVICE_NAME)) *out = &spatial;
    else if (!std::strcmp(name, SC_GAME_SPATIAL_NAME)) *out = &poses;
    else if (!std::strcmp(name, SC_ACTORS_NAME)) *out = &actors;
    else if (!std::strcmp(name, SC_ENTITIES_NAME)) *out = &entities;
    return *out ? SCO_OK : SCO_NOT_FOUND;
}

bool Open(Module& m, const char* path, uintptr_t token) {
    m.owner = reinterpret_cast<sco_plugin*>(token);
#if defined(_WIN32)
    HMODULE h = LoadLibraryA(path);
    m.handle = reinterpret_cast<void*>(h);
    auto symbol = [&](const char* n) { return h ? reinterpret_cast<void*>(GetProcAddress(h, n)) : nullptr; };
#else
    m.handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!m.handle) { std::printf("%s\n", dlerror()); return false; }
    auto symbol = [&](const char* n) { return dlsym(m.handle, n); };
#endif
    m.query = reinterpret_cast<sco_plugin_query_fn>(symbol("sco_plugin_query"));
    m.load = reinterpret_cast<sco_plugin_load_fn>(symbol("sco_plugin_load"));
    m.unload = reinterpret_cast<sco_plugin_unload_fn>(symbol("sco_plugin_unload"));
    return m.query && m.load && m.unload;
}
void Close(Module& m) {
#if defined(_WIN32)
    if (m.handle) FreeLibrary(reinterpret_cast<HMODULE>(m.handle));
#else
    if (m.handle) dlclose(m.handle);
#endif
}

// The path of the sender: about 6.4 m/s on foot, turning.
void Truth(double t, double p[3]) {
    p[0] = 6.0 * t + 1.5 * std::sin(0.9 * t);
    p[1] = 4.0 * std::sin(0.55 * t);
    p[2] = 0.5 * std::sin(0.3 * t);
}
constexpr double kMeanSpeed = 6.4;   // m/s, the mean speed of Truth

struct Packet { double due; unsigned k; Bytes bytes; };

struct Result {
    unsigned frames = 0, freezes = 0, jumps = 0;
    double frozenMs = 0, maxJump = 0, rms = 0, lagMs = 0;
};

// Frames from 3 s to 20 s. A freeze: at least 4 frames in a row (67 ms) in which the ghost moves less than a
// quarter of what the player does. A jump: a frame in which it moves more than 3 times that.
Result Measure(const Module& m) {
    Result r;
    unsigned run = 0;
    double runMs = 0;
    const auto endRun = [&]() { if (run >= 4) { ++r.freezes; r.frozenMs += runMs; } run = 0; runMs = 0; };
    std::vector<const Frame*> inRange;
    for (const Frame& f : m.frames) if (f.ms >= 3000 && f.ms <= 20000) inRange.push_back(&f);
    for (size_t i = 1; i < inRange.size(); ++i) {
        const double dt = inRange[i]->ms - inRange[i - 1]->ms;
        if (dt <= 0 || dt > 50) { endRun(); continue; }   // the test process itself stalled: not the module's motion
        double d = 0;
        for (int j = 0; j < 3; ++j) d += (inRange[i]->pos[j] - inRange[i - 1]->pos[j]) * (inRange[i]->pos[j] - inRange[i - 1]->pos[j]);
        d = std::sqrt(d);
        const double expected = kMeanSpeed * dt / 1000.0;
        ++r.frames;
        if (d < 0.25 * expected) { ++run; runMs += dt; } else endRun();
        if (d > 3.0 * expected) ++r.jumps;
        r.maxJump = std::max(r.maxJump, d);
    }
    endRun();
    // The best delay: the ghost at time t is where the player was at t - lag.
    double best = 1e30;
    for (int lag = 0; lag <= 800; lag += 10) {
        double sum = 0;
        for (const Frame* f : inRange) {
            double p[3];
            Truth((f->ms - lag) / 1000.0, p);
            double d = 0;
            for (int j = 0; j < 3; ++j) d += (f->pos[j] - p[j]) * (f->pos[j] - p[j]);
            sum += d;
        }
        const double rms = inRange.empty() ? 1e30 : std::sqrt(sum / inRange.size());
        if (rms < best) { best = rms; r.lagMs = lag; }
    }
    r.rms = best;
    return r;
}

void Report(const char* label, const Result& r) {
    std::printf("MOTION %-10s frames %u  freezes %u (%.0f ms frozen)  jumps %u  largest step %.2f m  fits the path to %.2f m at a delay of %.0f ms\n",
                label, r.frames, r.freezes, r.frozenMs, r.jumps, r.maxJump, r.rms, r.lagMs);
}

void RunModules(const char* adaptedPath, const char* baselinePath) {
    Module adapted, baseline;
    adapted.label = "adapted";
    baseline.label = "unmodified";
    modules.push_back(&adapted);
    CHECK(Open(adapted, adaptedPath, 1001));
    const bool haveBaseline = baselinePath != nullptr;
    if (haveBaseline) {
        modules.push_back(&baseline);
        CHECK(Open(baseline, baselinePath, 1002));
    }
    if (!adapted.query || (haveBaseline && !baseline.query)) return;
    CHECK(!std::strcmp(adapted.query()->name, "multiplayer"));

    sco_api api{};
    api.size = sizeof(api); api.major = SCO_API_MAJOR; api.minor = SCO_API_MINOR;
    api.register_command = Register; api.subscribe = Subscribe; api.unsubscribe = Unsubscribe; api.query_service = Query; api.log = LogFn;
    net.size = sizeof(net); net.is_active = NetActive; net.self_peer = Self; net.get_peers = GetPeers; net.get_peer_name = PeerName;
    net.send_channel = Send; net.register_channel = RegisterChannel; net.unregister_channel = UnregisterChannel;
    broker.size = sizeof(broker); broker.get_state = BrokerState; broker.leave = BrokerLeave;
    spatial.size = sizeof(spatial); spatial.player_pose = PlayerPose;
    poses.size = sizeof(poses); poses.zone_record = ZoneRecord; poses.find_zone = FindZone; poses.pose_to_zone = Convert;
    actors.size = sizeof(actors); actors.spawn_npc = SpawnActor; actors.despawn = DespawnActor;
    entities.size = sizeof(entities); entities.alive = Alive; entities.set_transform = SetTransform;

    for (Module* m : modules) CHECK(m->load(&api, m->owner) == SCO_OK);
    for (Module* m : modules) CHECK(m->messages.count("multiplayer.pose") && m->messages.count("multiplayer.spawn") && m->events.count("tick"));

    // The sender's poses: one every 100 ms for 20 s, each reaching the player 60 ms later plus up to 35 ms
    // either way; 12 in 100 are lost, four in a row (80 to 83) are lost, and pose 120 is held up 160 ms so that
    // it arrives after pose 121. The same list goes to both modules.
    std::mt19937_64 rng(126);
    std::uniform_real_distribution<double> jitter(-35, 35), chance(0, 1);
    std::vector<Packet> packets;
    unsigned lost = 0;
    for (unsigned k = 1; k <= 200; ++k) {
        if ((k >= 80 && k <= 83) || chance(rng) < 0.12) { ++lost; continue; }
        const double sent = 100.0 * k;
        double due = sent + 60 + jitter(rng) + (k == 120 ? 160 : 0);
        double p[3];
        Truth(sent / 1000.0, p);
        mpwire::Pose pose = MakePose(k, p[0], p[1]);
        pose.lv[0].pos[2] = p[2];
        const double yaw = 0.3 * sent / 1000.0;
        pose.lv[0].rot[2] = std::sin(yaw / 2);
        pose.lv[0].rot[3] = std::cos(yaw / 2);
        uint8_t data[mpwire::kMaxPose];
        const size_t n = mpwire::EncodePose(pose, data, sizeof(data));
        CHECK(n > 0);
        packets.push_back({ due, k, Bytes(data, data + n) });
    }
    std::stable_sort(packets.begin(), packets.end(), [](const Packet& a, const Packet& b) { return a.due < b.due; });
    std::printf("MOTION path: %zu of 200 poses delivered (%u lost, a burst of 4), 60 ms +-35 ms, one pose reordered\n", packets.size(), lost);

    mpwire::Spawn spawn{};
    spawn.kind = mpwire::kAvatar;
    spawn.sessionId = mpwire::SessionId(2, mpwire::kAvatar);
    std::snprintf(spawn.cls, sizeof(spawn.cls), "Synthetic_Human");
    uint8_t spawnData[mpwire::kMaxSpawn];
    const size_t spawnLen = mpwire::EncodeSpawn(spawn, spawnData, sizeof(spawnData));
    for (Module* m : modules) m->messages.at("multiplayer.spawn")(2, spawnData, static_cast<uint32_t>(spawnLen), nullptr);

    t0 = Clock::now();
    size_t next = 0;
    for (unsigned frame = 0;; ++frame) {
        std::this_thread::sleep_until(t0 + std::chrono::microseconds(static_cast<long long>(frame) * 16667));
        const double now = SimMs();
        if (now > 21000) break;
        while (next < packets.size() && packets[next].due <= now) {
            for (Module* m : modules)
                m->messages.at("multiplayer.pose")(2, packets[next].bytes.data(), static_cast<uint32_t>(packets[next].bytes.size()), nullptr);
            ++next;
        }
        for (Module* m : modules) m->events.at("tick")("tick", nullptr, nullptr);
    }

    for (Module* m : modules) {
        CHECK(m->ghost != 0 && m->frames.size() > 900);   // the ghost was spawned and moved at every frame
        CHECK(m->sends > 100);                            // and the module kept sending its own poses at about 10 Hz
    }
    const Result a = Measure(adapted);
    Report("adapted", a);
    CHECK(a.frames > 800 && a.rms < 0.6 && a.lagMs >= 150 && a.lagMs <= 450);
    CHECK(a.freezes <= 3);
    if (haveBaseline) {
        const Result b = Measure(baseline);
        Report("unmodified", b);
        std::printf("MOTION result: freezes %u -> %u, frozen %.0f ms -> %.0f ms, jumps %u -> %u, largest step %.2f m -> %.2f m (unmodified -> adapted)\n",
                    b.freezes, a.freezes, b.frozenMs, a.frozenMs, b.jumps, a.jumps, b.maxJump, a.maxJump);
        CHECK(b.freezes >= 6);                      // the path is rough enough to show the unmodified module's freezes
        CHECK(a.freezes * 2 <= b.freezes);
        CHECK(a.frozenMs * 2 <= b.frozenMs);
        CHECK(a.jumps <= b.jumps);
        CHECK(a.maxJump < b.maxJump);
    }
    for (Module* m : modules) m->unload();
    for (Module* m : modules) Close(*m);
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) { std::fprintf(stderr, "usage: multiplayer-motion-test <adapted module> [<unmodified module>]\n"); return 2; }
    RUN(TestInterpolationAndGaps);
    RUN(TestJitterIsRemoved);
    RUN(TestExtrapolationIsCapped);
    RUN(TestResets);
    RUN(TestOrder);
    RUN(TestExtrapolationLeadIsCapped);
    RUN(TestStallsAndBursts);
    RUN(TestSlowSenderLeadIsCapped);
    RUN(TestSlowSenderOnTheNetwork);
    RUN(TestRateDropLeadIsCapped);
    RUN(TestBunchedAndChangingArrivals);
    RUN(TestYoungTracksSmooth);
    RUN(TestLongGaps);
    RUN(TestLongGapSpeedUsesEverySequenceStep);
    RUN(TestTeleportFollowsTheCadence);
    RUN(TestBurstIsNotATeleport);
    if (argc == 2 && !std::strcmp(argv[1], "--track-tests")) {   // the buffer alone, no module and no real time
        std::printf("multiplayer motion (buffer only): %u passed, %u failed\n", passed, failed);
        return failed ? 1 : 0;
    }
    RunModules(argv[1], argc == 3 ? argv[2] : nullptr);
    std::printf("multiplayer motion: %u passed, %u failed (synthetic public services; no sockets/game)\n", passed, failed);
    return failed ? 1 : 0;
}
