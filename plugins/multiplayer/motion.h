// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Ghost motion from 10 Hz pose samples, smoother than two samples and a clamp. Header only, standard
// headers and multiplayer_wire.h only: nothing here touches the host, a clock or a socket, so a test can
// feed it arrival times and read positions back.
//
// One Track per remote player. Samples go in as they arrive (Add); a position for the ghost comes out for
// any time (Sample). What it does:
//   * Sample buffer. The last kRing poses are kept, each with a time on the sender's timeline.
//   * Dejitter. The sender sends one pose per tick that is at least 100 ms after the last, so poses are
//     about evenly spaced on the sender's side, while the network moves each one later by a different
//     amount. The time of a sample is not its arrival but the point of a line fitted through the arrival
//     times of the last kFit samples against their sequence numbers; a lost pose leaves a gap in the
//     sequence, which the fit keeps. The line is clamped near the arrival times, so it can never drift away.
//   * Playout delay. The ghost is drawn kPlayoutDelayMs behind the present, so that the sample after the
//     drawn time has usually arrived, even after one lost pose; between two samples it moves linearly.
//   * Capped extrapolation. When the drawn time is past the newest sample (several poses lost in a row) the
//     ghost keeps its last velocity for at most kMaxExtrapolateMs, then holds. Rotation is never extrapolated.
//     The velocity is the distance between the last two samples over the time the SENDER took for them. The wire
//     carries no sender time, so that time is judged from what the track has seen, and is never less than
//       - kSenderStepMs per sequence step (the tick gate: one pose per tick, a tick at least 100 ms after the
//         last), counted without a cap, since a longer gap only lowers the speed;
//       - the timeline's own gap between the two samples;
//       - the cadence of the timeline fit, from kCadenceSamples poses on;
//       - the newest arrival gaps when they are clearly slower than a 10 Hz sender can look under 35 ms of
//         jitter: the newest pair, or the newest two or three intervals together, more than kSlowExcessMs
//         longer than kSenderStepMs per step. That is how a sender that drops under 10 Hz (a game under 10
//         frames a second) is noticed within a pose or two, before the fit has followed it. The two- and
//         three-interval spans are left out when the newest pair arrived together and a cadence is known: that
//         is a stall let go, not a slow sender.
//     It is never the difference of the two arrival times alone: poses delivered in the same frame (a burst
//     after a stall, the first two after a reset) arrive 1 ms apart and would give a speed 100 times too high.
//     When the newest poses arrived together (under kBunchedRatio of the floor) and no cadence is known yet,
//     nothing says whether the sender is a 10 Hz or a 5 Hz one, and the ghost holds at the newest sample instead
//     of guessing. This is a judgment, not a measurement: a rate that drops while the arrival times jitter can
//     still be led too far for a pose or two (plugins/multiplayer/README.md, Ghost motion, gives the measured
//     bounds). A second guard caps the distance itself at kMaxLeadSteps steps.
//   * Resets. The track starts over, and the ghost snaps to the new sample instead of gliding to it, when
//     nothing arrived for kStaleMs, when the pose's frames changed (another chain of zones, a ship boarded
//     or left), or when two samples are so far apart that they are a teleport (kTeleportSpeed over the time
//     the sender took: at least kSenderStepMs, or the known cadence, per sequence step, up to kMaxTeleportSteps).
//   * Order. A pose that is not newer than the last (wrap-aware) is dropped, as before.
#include "multiplayer_wire.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace mpmotion {

constexpr unsigned kRing = 12;               // samples kept: 1.2 s of poses
constexpr unsigned kFit = 16;                // samples in the timeline fit
constexpr double kPlayoutDelayMs = 200.0;    // how far behind the newest data the ghost is drawn
constexpr double kMaxExtrapolateMs = 150.0;  // longest the ghost moves on past the newest sample
constexpr double kSenderStepMs = 100.0;      // the sender sends at most one pose per 100 ms (its tick gate): the floor of the time one sequence step took
constexpr unsigned kCadenceSamples = 10;     // poses in the timeline fit before its slope counts as the sender's cadence: a fit through fewer says little (a few poses delivered together skew it)
constexpr unsigned kSlowSpanSteps = 3;       // newest sequence intervals whose arrival gaps are read for a sender slower than 10 Hz
constexpr double kSlowExcessMs = 75.0;       // an arrival span this much longer than kSenderStepMs per step is a slower sender, not jitter: 35 ms either way puts two poses of a 10 Hz sender at most 70 ms off
constexpr double kBunchedRatio = 0.3;        // an arrival span under this share of kSenderStepMs per step: the poses came together
constexpr double kMaxTeleportSteps = 50.0;   // sequence steps the teleport test allows between two samples (5 s of the sender's time): a fast player's long gap is no teleport, but no gap makes every jump pass
constexpr double kMaxLeadSteps = 1.5;        // the ghost never goes on past the newest sample by more than this many sequence steps of movement
constexpr uint64_t kStaleMs = 600;           // no pose for this long: the next one starts a new track
constexpr double kTeleportSpeed = 3000.0;    // m/s between two samples; faster is a teleport, not motion
constexpr double kMinSampleMs = 40.0;        // a fitted gap between consecutive poses outside this range is not trusted
constexpr double kMaxSampleMs = 400.0;
constexpr double kMaxFitErrorMs = 150.0;     // the fitted time stays within this of the arrival

enum class Added : uint8_t {
    Dropped,         // not newer than the last pose
    Appended,        // joined the track
    ResetStale,      // started over: nothing arrived for kStaleMs
    ResetFrame,      // started over: the zones the pose is in changed
    ResetTeleport,   // started over: the player jumped
};

inline bool SameFrame(const mpwire::Pose& a, const mpwire::Pose& b) {
    if (a.levels != b.levels || a.ship != b.ship || a.shipFirst != b.shipFirst) return false;
    for (unsigned i = 0; i < a.levels; ++i)
        if (std::strncmp(a.lv[i].name, b.lv[i].name, sizeof(a.lv[i].name)) != 0) return false;
    return true;
}

class Track {
public:
    void Clear() { count_ = 0; raw_count_ = 0; last_arrival_ = 0; cadence_ = 0; }
    bool Valid() const { return count_ > 0; }
    unsigned Count() const { return count_; }
    uint64_t LastArrivalMs() const { return last_arrival_; }
    // The newest pose (its frames name the zones to resolve), or null.
    const mpwire::Pose* Newest() const { return count_ ? &s_[count_ - 1].pose : nullptr; }
    // Time of sample i on the sender's timeline, in ms on the arrival clock; i < Count().
    double TimeOf(unsigned i) const { return s_[i].t; }

    Added Add(const mpwire::Pose& pose, uint64_t arrival_ms) {
        const bool stale = count_ && arrival_ms - last_arrival_ > kStaleMs;
        if (count_ && !stale) {
            const uint32_t delta = pose.seq - last_seq_;
            if (!delta || delta >= 0x80000000u) return Added::Dropped;
        }
        Added result = Added::Appended;
        if (stale) { Clear(); result = Added::ResetStale; }
        const bool had = count_ > 0;
        if (had && !SameFrame(s_[count_ - 1].pose, pose)) { Clear(); result = Added::ResetFrame; }
        const bool had_now = count_ > 0;
        uint64_t seq64 = had_now ? last_seq64_ + static_cast<uint32_t>(pose.seq - last_seq_) : pose.seq;
        double t = Timeline(seq64, arrival_ms);
        if (had_now) {
            t = std::max(t, s_[count_ - 1].t + 1.0);   // only to keep the samples in order; no speed is ever worked out from it
            if (Teleported(s_[count_ - 1].pose, pose, t - s_[count_ - 1].t, seq64 - s_[count_ - 1].seq)) {
                Clear();
                result = Added::ResetTeleport;
                t = static_cast<double>(arrival_ms);
                seq64 = pose.seq;                 // a new track counts from this pose, as after any other reset
                raw_count_ = 0;
                Timeline(seq64, arrival_ms);      // restart the fit with this sample alone
            }
        }
        if (count_ == kRing) {
            std::copy(s_.begin() + 1, s_.end(), s_.begin());
            --count_;
        }
        s_[count_].pose = pose;
        s_[count_].t = t;
        s_[count_].seq = seq64;
        s_[count_].arrival = static_cast<double>(arrival_ms);
        ++count_;
        last_seq_ = pose.seq;
        last_seq64_ = count_ == 1 ? pose.seq : seq64;
        last_arrival_ = arrival_ms;
        return result;
    }

    // Where the ghost is at now_ms: the player's pose in level `level` of the newest pose (ship: the ship's
    // pose in that level; the level must be at least the pose's shipFirst). false with no sample.
    bool Sample(uint64_t now_ms, bool ship, unsigned level, double pos[3], double rot[4]) const {
        if (!count_ || level >= static_cast<unsigned>(mpwire::kMaxLevels)) return false;
        const double rt = static_cast<double>(now_ms) - kPlayoutDelayMs;
        const Entry& last = s_[count_ - 1];
        if (count_ == 1 || rt <= s_[0].t) { Read(s_[0].pose, ship, level, pos, rot); return true; }
        if (rt >= last.t) {
            const Entry& before = s_[count_ - 2];
            double before_pos[3], before_rot[4];
            Read(last.pose, ship, level, pos, rot);
            // No evidence of how long the sender takes (the newest poses came together and the track is too young to
            // know its cadence): the ghost holds. The sender's minimum is a floor, not an answer: a sender slower than
            // 10 Hz would be led too far ahead.
            double per_step;
            if (!SenderStepMs(&per_step)) return true;
            Read(before.pose, ship, level, before_pos, before_rot);
            // The time the sender took for the last step: what the timeline says, but never less than its time per
            // sequence step. (Arrival times of poses delivered together are 1 ms apart; the sender's were not.)
            const double steps = SeqSteps(last.seq - before.seq);
            const double gap = std::max(last.t - before.t, per_step * steps);
            const double ahead = std::min(rt - last.t, kMaxExtrapolateMs);
            double lead[3], length = 0, stepLength = 0;
            for (int j = 0; j < 3; ++j) {
                const double d = pos[j] - before_pos[j];
                lead[j] = d / gap * ahead;
                length += lead[j] * lead[j];
                stepLength += d * d;
            }
            length = std::sqrt(length);
            const double cap = kMaxLeadSteps * std::sqrt(stepLength) / steps;   // a second guard on the distance
            const double scale = length > cap * (1.0 + 1e-9) ? cap / length : 1.0;
            for (int j = 0; j < 3; ++j) pos[j] += lead[j] * scale;
            return true;
        }
        unsigned k = 0;
        while (k + 2 < count_ && s_[k + 1].t <= rt) ++k;   // s_[k].t <= rt < s_[k + 1].t
        double ap[3], aq[4], bp[3], bq[4];
        Read(s_[k].pose, ship, level, ap, aq);
        Read(s_[k + 1].pose, ship, level, bp, bq);
        const double u = std::min(1.0, std::max(0.0, (rt - s_[k].t) / (s_[k + 1].t - s_[k].t)));
        double dot = 0;
        for (int j = 0; j < 4; ++j) dot += aq[j] * bq[j];
        for (int j = 0; j < 3; ++j) pos[j] = ap[j] + u * (bp[j] - ap[j]);
        for (int j = 0; j < 4; ++j) rot[j] = (dot < 0 ? -aq[j] : aq[j]) * (1 - u) + bq[j] * u;
        return true;
    }

private:
    struct Entry { mpwire::Pose pose; double t; uint64_t seq; double arrival; };   // seq: the sender's sequence number, counted past a wrap
    struct Raw { uint64_t seq; double arrival; };

    // Sequence steps between two samples, at least 1. Not capped: a longer gap means the sender took longer, which only
    // lowers the speed (the teleport test bounds it by itself).
    static double SeqSteps(uint64_t difference) {
        return std::max(static_cast<double>(difference), 1.0);
    }

    // Milliseconds the sender takes per sequence step, as far as the track can tell (see the header); false: it cannot, the
    // newest poses came together and the cadence is not known. Needs two samples.
    bool SenderStepMs(double* per_step) const {
        const Entry& last = s_[count_ - 1];
        const Entry& before = s_[count_ - 2];
        double per = std::max(kSenderStepMs, cadence_);
        const bool together = last.arrival - before.arrival < kBunchedRatio * kSenderStepMs * SeqSteps(last.seq - before.seq);
        for (unsigned m = 1; m <= kSlowSpanSteps && m < count_; ++m) {
            if (m > 1 && together && cadence_ > 0) break;   // a stall let go, not a slow sender
            const Entry& first = s_[count_ - 1 - m];
            const double steps = SeqSteps(last.seq - first.seq);
            const double span = last.arrival - first.arrival;
            if (span - kSenderStepMs * steps > kSlowExcessMs) per = std::max(per, span / steps);
        }
        if (cadence_ <= 0) {
            const Entry& first = s_[count_ - 1 - std::min(kSlowSpanSteps, count_ - 1)];
            if (last.arrival - first.arrival < kBunchedRatio * kSenderStepMs * SeqSteps(last.seq - first.seq)) return false;
        }
        *per_step = per;
        return true;
    }

    static void Read(const mpwire::Pose& p, bool ship, unsigned level, double pos[3], double rot[4]) {
        std::memcpy(pos, ship ? p.shipPos[level] : p.lv[level].pos, 3 * sizeof(double));
        std::memcpy(rot, ship ? p.shipRot[level] : p.lv[level].rot, 4 * sizeof(double));
    }

    // Records this pose's (sequence, arrival) and answers the time it has on the sender's timeline: the
    // least-squares line of arrival over sequence through the last kFit points, read at this sequence.
    double Timeline(uint64_t seq, uint64_t arrival_ms) {
        cadence_ = 0;   // read again from this fit, if it has enough poses
        if (raw_count_ == kFit) {
            std::copy(raw_.begin() + 1, raw_.end(), raw_.begin());
            --raw_count_;
        }
        raw_[raw_count_++] = Raw{ seq, static_cast<double>(arrival_ms) };
        const double arrival = static_cast<double>(arrival_ms);
        if (raw_count_ < 3) return arrival;
        double mx = 0, my = 0;
        for (unsigned i = 0; i < raw_count_; ++i) {
            mx += static_cast<double>(raw_[i].seq - raw_[0].seq);
            my += raw_[i].arrival - raw_[0].arrival;
        }
        mx /= raw_count_;
        my /= raw_count_;
        double sxx = 0, sxy = 0;
        for (unsigned i = 0; i < raw_count_; ++i) {
            const double dx = static_cast<double>(raw_[i].seq - raw_[0].seq) - mx;
            sxx += dx * dx;
            sxy += dx * (raw_[i].arrival - raw_[0].arrival - my);
        }
        if (sxx <= 0) return arrival;
        const double slope = sxy / sxx;
        if (!(slope >= kMinSampleMs && slope <= kMaxSampleMs)) return arrival;
        if (raw_count_ >= kCadenceSamples) cadence_ = slope;   // ms the sender takes per sequence step
        const double fitted = raw_[0].arrival + my + slope * (static_cast<double>(seq - raw_[0].seq) - mx);
        return std::fabs(fitted - arrival) > kMaxFitErrorMs ? arrival : fitted;
    }

    // Two samples further apart than a player can move in the time the sender took between them: at least
    // kSenderStepMs per sequence step (its cadence, when the track knows it: 2.5 km/s at 200 ms a pose is 500 m a step, no
    // teleport), whatever the arrival times say. The steps count up to kMaxTeleportSteps: enough for a fast player's long
    // gap (2.5 km/s over 10 steps), too few for any jump to pass.
    bool Teleported(const mpwire::Pose& a, const mpwire::Pose& b, double dt_ms, uint64_t seq_difference) const {
        const double per_step = std::max(kSenderStepMs, cadence_);
        const double seconds = std::max(dt_ms, per_step * std::min(SeqSteps(seq_difference), kMaxTeleportSteps)) / 1000.0;
        const double limit = kTeleportSpeed * seconds;
        for (unsigned i = 0; i < b.levels; ++i) {
            if (Far(a.lv[i].pos, b.lv[i].pos, limit)) return true;
            if (b.ship && i >= b.shipFirst && Far(a.shipPos[i], b.shipPos[i], limit)) return true;
        }
        return false;
    }
    static bool Far(const double* a, const double* b, double limit) {
        double d = 0;
        for (int j = 0; j < 3; ++j) d += (a[j] - b[j]) * (a[j] - b[j]);
        return d > limit * limit;
    }

    std::array<Entry, kRing> s_{};
    unsigned count_ = 0;
    std::array<Raw, kFit> raw_{};
    unsigned raw_count_ = 0;
    double cadence_ = 0;   // ms per sequence step from the fit; 0 while the track cannot tell
    uint32_t last_seq_ = 0;
    uint64_t last_seq64_ = 0;
    uint64_t last_arrival_ = 0;
};

}  // namespace mpmotion
