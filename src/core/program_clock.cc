// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/program_clock.hpp"

#include <cassert>
#include <optional>

#include "core/ts_utils.hpp"
#include "tsduck/tsTOT.h"
#include "tsduck/tsTS.h"

namespace aribcap_dump {
namespace {

// Number of 27 MHz PCR ticks per 90 kHz PTS/DTS tick. Dividing a raw PCR by this converts it
// to the 90 kHz unit PTS uses; `last_pcr_90k_` holds the result and compares directly with a
// PTS value.
constexpr std::int64_t kPcrTicksPerPtsTick = 300;
// PTS/DTS clock rate, used to convert a tick delta to ms
constexpr std::int64_t kPtsTicksPerSecond = 90'000;
constexpr std::int64_t kMillisPerSecond = 1'000;

// Largest forward PCR step still treated as normal progress. The 1 s bound sits far above any
// normal PCR step, wide enough that a benign jump is not misread as a discontinuity. This tool
// needs clock sync for timestamps, not TR 101 290 conformance.
constexpr std::int64_t kMaxSanePcrStep90k = 90'000;
// Number of consecutive out-of-series PCRs that confirm an unflagged discontinuity and trigger
// re-sync.
constexpr int kUnflaggedDiscontinuityConfirmCount = 2;

// Computes the signed 33-bit wrap-aware difference (to - from) in 90 kHz ticks, picking the
// nearer direction around the 2^33 PTS/PCR wrap.
//
// Requires both operands already within the 33-bit PTS/DTS range.
[[nodiscard]] std::int64_t WrapAwareDelta90k(std::int64_t from, std::int64_t to) {
    assert(static_cast<std::uint64_t>(from) <= ts::MAX_PTS_DTS);
    assert(static_cast<std::uint64_t>(to) <= ts::MAX_PTS_DTS);

    const auto forward =
        ts::DiffPTS(static_cast<std::uint64_t>(from), static_cast<std::uint64_t>(to));
    const auto backward =
        ts::DiffPTS(static_cast<std::uint64_t>(to), static_cast<std::uint64_t>(from));

    return forward <= backward ? static_cast<std::int64_t>(forward)
                               : -static_cast<std::int64_t>(backward);
}

// Returns true when a PCR step advances forward by at most one second.
[[nodiscard]] bool IsSanePcrStep(std::int64_t delta_90k) {
    return delta_90k >= 0 && delta_90k <= kMaxSanePcrStep90k;
}

}  // namespace

void ProgramClock::SetPcrPid(ts::PID pid) {
    if (pid == ts::PID_NULL) {
        return;
    }

    if (pcr_pid_ == pid) {
        return;
    }

    pcr_pid_ = pid;
    last_pcr_90k_.reset();
    reference_point_.reset();
    ClearSuspect();
}

std::optional<PcrDiscontinuity> ProgramClock::RecordPcr(const ts::TSPacket& packet) {
    if (!pcr_pid_.has_value() || packet.getPID() != *pcr_pid_ || !packet.hasPCR()) {
        return std::nullopt;
    }

    const auto pcr = packet.getPCR();

    if (pcr == ts::INVALID_PCR || pcr > ts::MAX_PCR) {
        return std::nullopt;
    }

    const auto pcr_90k = static_cast<std::int64_t>(pcr) / kPcrTicksPerPtsTick;

    // When discontinuity_indicator is set, the old PCR<->wall-time mapping is no longer valid.
    if (packet.getDiscontinuityIndicator()) {
        AdoptNewPcrSeries(pcr_90k);

        return PcrDiscontinuity{static_cast<std::uint16_t>(*pcr_pid_), true};
    }

    if (!last_pcr_90k_.has_value()) {
        last_pcr_90k_ = pcr_90k;
        ClearSuspect();

        return std::nullopt;
    }

    const auto step = WrapAwareDelta90k(*last_pcr_90k_, pcr_90k);

    if (IsSanePcrStep(step)) {
        last_pcr_90k_ = pcr_90k;
        ClearSuspect();

        return std::nullopt;
    }

    // Unflagged reverse or oversized jump: don't corrupt the reference on a single bad PCR.
    return HandleUnflaggedPcrDiscontinuity(pcr_90k);
}

void ProgramClock::AdoptNewPcrSeries(std::int64_t pcr_90k) {
    last_pcr_90k_ = pcr_90k;
    reference_point_.reset();
    ClearSuspect();
}

std::optional<PcrDiscontinuity> ProgramClock::HandleUnflaggedPcrDiscontinuity(
    std::int64_t pcr_90k) {
    ++suspect_pcr_count_;

    if (suspect_pcr_count_ >= kUnflaggedDiscontinuityConfirmCount) {
        AdoptNewPcrSeries(pcr_90k);

        return PcrDiscontinuity{static_cast<std::uint16_t>(*pcr_pid_), false};
    }

    return std::nullopt;
}

void ProgramClock::ClearSuspect() {
    suspect_pcr_count_ = 0;
}

void ProgramClock::UpdateReferencePointFromTot(const ts::TOT& tot) {
    if (!tot.isValid()) {
        return;
    }

    const auto unix_ms = JstTimeToUnixMs(tot.utc_time);

    if (!unix_ms.has_value()) {
        return;
    }

    // While a suspect PCR is unresolved, the current series is untrusted. Don't anchor a new
    // reference to the (possibly stale) last accepted PCR until the anomaly clears.
    if (!last_pcr_90k_.has_value() || suspect_pcr_count_ > 0) {
        return;
    }

    // `unix_ms` is the wall-clock time the TOT itself carries.
    // `last_pcr_90k_` is the most recent PCR observed so far, arriving on its own cadence
    // rather than at that same time.
    //
    // Pairing the two inevitably introduces a systematic offset into the reference point (up to
    // one PCR interval, ~100 ms for ARIB broadcasts).
    reference_point_ = ReferencePoint{
        .pcr_90k = *last_pcr_90k_,
        .unix_ms = *unix_ms,
    };
}

std::optional<std::int64_t> ProgramClock::PtsToUnixMs(std::uint64_t pts_90k) const {
    // A pending suspect means the reference may belong to a superseded series; suppress the
    // conversion until the anomaly resolves rather than return a plausible but wrong time.
    if (!reference_point_.has_value() || suspect_pcr_count_ > 0 || pts_90k > ts::MAX_PTS_DTS) {
        return std::nullopt;
    }

    const auto delta_90k =
        WrapAwareDelta90k(reference_point_->pcr_90k, static_cast<std::int64_t>(pts_90k));
    const auto offset_ms = (delta_90k * kMillisPerSecond) / kPtsTicksPerSecond;

    return reference_point_->unix_ms + offset_ms;
}

}  // namespace aribcap_dump
