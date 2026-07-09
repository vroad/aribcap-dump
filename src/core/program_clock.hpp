// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <optional>

#include "core/output_record.hpp"
#include "tsduck/tsTOT.h"
#include "tsduck/tsTS.h"
#include "tsduck/tsTSPacket.h"

namespace aribcap_dump {

class ProgramClockTestAccessor;

// PCR/TOT tracker for converting PTS timestamps to Unix milliseconds.
class ProgramClock {
   public:
    // Sets the PCR PID to track.
    // Changing PIDs discards any previously recorded PCR/reference.
    void SetPcrPid(ts::PID pid);
    // Stores the packet's PCR, converted to 90 kHz ticks, as the latest recorded PCR.
    //
    // Returns nullopt for normal PCR progression. Returns `PcrDiscontinuity` for a discontinuity
    // flagged by `discontinuity_indicator` or for an inferred discontinuity.
    [[nodiscard]] std::optional<PcrDiscontinuity> RecordPcr(const ts::TSPacket& packet);
    // Updates the PTS conversion reference point from an ISDB TOT and the latest recorded PCR.
    void UpdateReferencePointFromTot(const ts::TOT& tot);

    // Converts a 90 kHz PTS timestamp to Unix milliseconds using the reference
    // point.
    [[nodiscard]] std::optional<std::int64_t> PtsToUnixMs(std::uint64_t pts_90k) const;

   private:
    friend class ProgramClockTestAccessor;

    // Adopts `pcr_90k` as the base of a new PCR series immediately. The old PCR<->wall-time
    // mapping is dropped. PTS-to-Unix conversion remains unavailable until the next TOT.
    void AdoptNewPcrSeries(std::int64_t pcr_90k);
    // Handles a suspect PCR and adopts it as an inferred discontinuity after repeated samples.
    //
    // Returns a `PcrDiscontinuity` when the suspect PCR is adopted as an inferred discontinuity;
    // otherwise nullopt.
    [[nodiscard]] std::optional<PcrDiscontinuity> HandleSuspectPcrDiscontinuity(
        std::int64_t pcr_90k);
    void ClearSuspect();

    // Reference PCR/wall-clock pair used to convert PTS timestamps to Unix milliseconds.
    struct ReferencePoint {
        std::int64_t pcr_90k = 0;
        std::int64_t unix_ms = 0;
    };

    std::optional<ts::PID> pcr_pid_;
    // Last accepted PCR sample, used to validate PCR continuity even before any TOT arrives.
    std::optional<std::int64_t> last_pcr_90k_;
    // PCR/wall-clock reference used for PTS conversion, created only after both PCR and TOT arrive.
    std::optional<ReferencePoint> reference_point_;

    // Number of consecutive PCRs with a delta over 1s or backward movement from the accepted PCR
    // series.
    int suspect_pcr_count_ = 0;
};

}  // namespace aribcap_dump
