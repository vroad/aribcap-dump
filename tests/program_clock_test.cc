// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/program_clock.hpp"

#include <cstdint>
#include <optional>

#include "tests/tot_test_utils.hpp"
#include "tsduck/tsBinaryTable.h"
#include "tsduck/tsDuckContext.h"
#include "tsduck/tsTOT.h"
#include "tsduck/tsTSPacket.h"

#include <catch2/catch_test_macros.hpp>

namespace aribcap_dump {

// Test-only accessor: exposes ProgramClock's private state so tests can assert on it directly.
class ProgramClockTestAccessor {
   public:
    static std::optional<std::int64_t> LastPcr90k(const ProgramClock& clock) {
        return clock.last_pcr_90k_;
    }

    static bool HasReference(const ProgramClock& clock) {
        return clock.reference_point_.has_value();
    }

    static int SuspectPcrCount(const ProgramClock& clock) {
        return clock.suspect_pcr_count_;
    }
};

}  // namespace aribcap_dump

namespace {

constexpr ts::PID kPcrPid = 0x100;

// Builds a TS packet on kPcrPid carrying pcr_90k (converted to 27 MHz PCR units) and optionally
// an adaptation-field discontinuity_indicator.
ts::TSPacket MakePcrPacket(std::int64_t pcr_90k, bool discontinuity = false) {
    ts::TSPacket packet;
    packet.init(kPcrPid);
    REQUIRE(packet.setPCR(static_cast<std::uint64_t>(pcr_90k) * 300, true));

    if (discontinuity) {
        REQUIRE(packet.setDiscontinuityIndicator());
    }

    return packet;
}

using aribcap_dump::test::MakeTot;

class ProgramClockFixture {
   public:
    aribcap_dump::ProgramClock clock;

    void SetPcrPid() {
        clock.SetPcrPid(kPcrPid);
    }

    std::optional<aribcap_dump::PcrDiscontinuity> RecordPcr(std::int64_t pcr_90k,
                                                            bool discontinuity = false) {
        return clock.RecordPcr(MakePcrPacket(pcr_90k, discontinuity));
    }

    void AcceptPcr(std::int64_t pcr_90k, bool discontinuity = false) {
        static_cast<void>(RecordPcr(pcr_90k, discontinuity));
    }

    void RecordTot(std::int64_t unix_ms) {
        clock.UpdateReferencePointFromTot(MakeTot(unix_ms));
    }
};

}  // namespace

// -------------------------------------------------------------------------------------------------
// PTS conversion tests
// -------------------------------------------------------------------------------------------------

TEST_CASE_METHOD(ProgramClockFixture, "PtsToUnixMs requires both PCR and TOT before conversion") {
    SECTION("no PCR or TOT") {
        CHECK_FALSE(clock.PtsToUnixMs(990'000).has_value());
    }

    SECTION("TOT without PCR") {
        RecordTot(1'700'000'000'000);

        CHECK_FALSE(clock.PtsToUnixMs(990'000).has_value());
    }
}

TEST_CASE_METHOD(ProgramClockFixture, "PtsToUnixMs converts PTS using the PCR/TOT reference") {
    constexpr std::int64_t kWallBase = 1'700'000'000'000;

    SetPcrPid();
    AcceptPcr(900'000);
    RecordTot(kWallBase);

    SECTION("PTS after the reference PCR") {
        const auto plus_one_second = clock.PtsToUnixMs(990'000);

        REQUIRE(plus_one_second.has_value());
        CHECK(*plus_one_second == kWallBase + 1'000);
    }

    SECTION("PTS before the reference PCR") {
        const auto minus_half_second = clock.PtsToUnixMs(855'000);

        REQUIRE(minus_half_second.has_value());
        CHECK(*minus_half_second == kWallBase - 500);
    }
}

TEST_CASE_METHOD(ProgramClockFixture,
                 "PtsToUnixMs converts ISDB TOT from JST to Unix milliseconds") {
    constexpr std::int64_t kPcr90k = 900'000;
    constexpr std::int64_t kJstMidnightUnixMs = 1'577'804'400'000;

    ts::DuckContext context;

    SetPcrPid();
    AcceptPcr(kPcr90k);

    ts::TOT tot(ts::Time(2020, 1, 1, 0, 0, 0));
    ts::BinaryTable table;
    REQUIRE(tot.serialize(context, table));
    clock.UpdateReferencePointFromTot(ts::TOT(context, table));

    const auto result = clock.PtsToUnixMs(kPcr90k);
    REQUIRE(result.has_value());
    CHECK(*result == kJstMidnightUnixMs);
}

TEST_CASE_METHOD(ProgramClockFixture, "PtsToUnixMs handles 33-bit PTS wrap-around") {
    constexpr std::int64_t kPtsModulus = std::int64_t{1} << 33;
    constexpr std::int64_t kRefPcr90k = kPtsModulus - 90'000;  // 1 s before wrap
    constexpr std::uint64_t kPtsAfterWrap = 90'000;            // 1 s after wrap
    constexpr std::int64_t kWallBase = 1'700'000'000'000;

    SetPcrPid();
    AcceptPcr(kRefPcr90k);
    RecordTot(kWallBase);

    const auto result = clock.PtsToUnixMs(kPtsAfterWrap);
    REQUIRE(result.has_value());
    CHECK(*result == kWallBase + 2'000);
}

// -------------------------------------------------------------------------------------------------
// PCR continuity tests
// -------------------------------------------------------------------------------------------------

TEST_CASE_METHOD(ProgramClockFixture, "RecordPcr accepts in-series PCR samples") {
    SetPcrPid();

    SECTION("normal increasing PCR") {
        constexpr std::int64_t kWall = 1'700'000'000'000;

        AcceptPcr(900'000);
        CHECK(aribcap_dump::ProgramClockTestAccessor::LastPcr90k(clock) == 900'000);

        AcceptPcr(903'000);  // +3'000 ticks: normal forward step
        CHECK(aribcap_dump::ProgramClockTestAccessor::LastPcr90k(clock) == 903'000);
        CHECK(aribcap_dump::ProgramClockTestAccessor::SuspectPcrCount(clock) == 0);

        RecordTot(kWall);
        const auto plus_one_second = clock.PtsToUnixMs(903'000 + 90'000);
        REQUIRE(plus_one_second.has_value());
        CHECK(*plus_one_second == kWall + 1'000);
    }

    SECTION("33-bit PCR wrap") {
        constexpr std::int64_t kModulus = std::int64_t{1} << 33;
        constexpr std::int64_t kNearTop = kModulus - 45'000;  // 0.5 s before wrap
        constexpr std::int64_t kAfterWrap = 45'000;           // 0.5 s after wrap

        AcceptPcr(kNearTop);
        CHECK(aribcap_dump::ProgramClockTestAccessor::LastPcr90k(clock) == kNearTop);

        AcceptPcr(kAfterWrap);  // +90'000 ticks across the wrap
        CHECK(aribcap_dump::ProgramClockTestAccessor::LastPcr90k(clock) == kAfterWrap);
        CHECK(aribcap_dump::ProgramClockTestAccessor::SuspectPcrCount(clock) == 0);
    }
}

// -------------------------------------------------------------------------------------------------
// Flagged discontinuity tests
// -------------------------------------------------------------------------------------------------

TEST_CASE_METHOD(ProgramClockFixture, "RecordPcr handles flagged discontinuities") {
    SetPcrPid();

    SECTION("reverse PCR with a flagged discontinuity is adopted immediately") {
        AcceptPcr(900'000);
        REQUIRE(aribcap_dump::ProgramClockTestAccessor::LastPcr90k(clock) == 900'000);

        AcceptPcr(100, /*discontinuity=*/true);
        CHECK(aribcap_dump::ProgramClockTestAccessor::LastPcr90k(clock) == 100);
    }

    SECTION("reference is reset until the next TOT") {
        constexpr std::int64_t kWall0 = 1'700'000'000'000;
        constexpr std::int64_t kWall1 = 1'700'000'100'000;

        AcceptPcr(900'000);
        RecordTot(kWall0);
        REQUIRE(aribcap_dump::ProgramClockTestAccessor::HasReference(clock));
        REQUIRE(clock.PtsToUnixMs(900'000).has_value());

        AcceptPcr(100, /*discontinuity=*/true);
        CHECK_FALSE(aribcap_dump::ProgramClockTestAccessor::HasReference(clock));
        CHECK_FALSE(clock.PtsToUnixMs(100).has_value());

        RecordTot(kWall1);
        REQUIRE(aribcap_dump::ProgramClockTestAccessor::HasReference(clock));
        const auto plus_one_second = clock.PtsToUnixMs(100 + 90'000);
        REQUIRE(plus_one_second.has_value());
        CHECK(*plus_one_second == kWall1 + 1'000);
    }

    SECTION("flagged discontinuity is reported via the return value") {
        CHECK_FALSE(RecordPcr(900'000).has_value());  // first PCR: no re-sync
        CHECK_FALSE(RecordPcr(903'000).has_value());  // normal in-series step

        const auto discontinuity = RecordPcr(100, /*discontinuity=*/true);
        REQUIRE(discontinuity.has_value());
        CHECK(discontinuity->pid == kPcrPid);
        CHECK(discontinuity->flagged);
    }
}

// -------------------------------------------------------------------------------------------------
// Inferred discontinuity tests
// -------------------------------------------------------------------------------------------------

TEST_CASE_METHOD(ProgramClockFixture, "RecordPcr infers discontinuities from out-of-series PCRs") {
    SetPcrPid();

    SECTION("a lone reverse PCR is only held as suspect") {
        AcceptPcr(900'000);
        AcceptPcr(100);

        CHECK(aribcap_dump::ProgramClockTestAccessor::LastPcr90k(clock) == 900'000);
        CHECK(aribcap_dump::ProgramClockTestAccessor::SuspectPcrCount(clock) == 1);
    }

    SECTION("repeated out-of-series PCRs re-sync to the latest PCR") {
        constexpr std::int64_t kWall = 1'700'000'000'000;

        AcceptPcr(900'000);
        RecordTot(kWall);
        REQUIRE(aribcap_dump::ProgramClockTestAccessor::HasReference(clock));

        AcceptPcr(100);  // suspect count 1; reference left intact
        CHECK(aribcap_dump::ProgramClockTestAccessor::SuspectPcrCount(clock) == 1);
        CHECK(aribcap_dump::ProgramClockTestAccessor::HasReference(clock));

        AcceptPcr(50);  // second out-of-series PCR: confirm and re-sync
        CHECK(aribcap_dump::ProgramClockTestAccessor::LastPcr90k(clock) == 50);
        CHECK(aribcap_dump::ProgramClockTestAccessor::SuspectPcrCount(clock) == 0);
        CHECK_FALSE(aribcap_dump::ProgramClockTestAccessor::HasReference(clock));
        CHECK_FALSE(clock.PtsToUnixMs(50).has_value());
    }

    SECTION("returning to the normal series clears the suspect state") {
        AcceptPcr(900'000);
        AcceptPcr(100);
        REQUIRE(aribcap_dump::ProgramClockTestAccessor::SuspectPcrCount(clock) == 1);

        AcceptPcr(903'000);
        CHECK(aribcap_dump::ProgramClockTestAccessor::SuspectPcrCount(clock) == 0);
        CHECK(aribcap_dump::ProgramClockTestAccessor::LastPcr90k(clock) == 903'000);

        AcceptPcr(100);
        CHECK(aribcap_dump::ProgramClockTestAccessor::SuspectPcrCount(clock) == 1);
        CHECK(aribcap_dump::ProgramClockTestAccessor::LastPcr90k(clock) == 903'000);
    }

    SECTION("TOT arriving while suspect is not paired with the old PCR") {
        constexpr std::int64_t kWall0 = 1'700'000'000'000;
        constexpr std::int64_t kWall1 = 1'700'000'050'000;
        constexpr std::int64_t kWall2 = 1'700'000'100'000;

        AcceptPcr(900'000);
        RecordTot(kWall0);
        REQUIRE(aribcap_dump::ProgramClockTestAccessor::HasReference(clock));
        REQUIRE(clock.PtsToUnixMs(900'000) == kWall0);

        AcceptPcr(100);
        CHECK_FALSE(clock.PtsToUnixMs(900'000).has_value());

        RecordTot(kWall1);
        AcceptPcr(903'000);
        REQUIRE(clock.PtsToUnixMs(900'000) == kWall0);

        RecordTot(kWall2);
        REQUIRE(clock.PtsToUnixMs(903'000) == kWall2);
    }

    SECTION("conversion is suppressed while suspect and restored once it clears") {
        constexpr std::int64_t kWall = 1'700'000'000'000;

        AcceptPcr(900'000);
        RecordTot(kWall);
        REQUIRE(clock.PtsToUnixMs(900'000) == kWall);

        AcceptPcr(100);
        CHECK_FALSE(clock.PtsToUnixMs(900'000).has_value());

        AcceptPcr(903'000);
        CHECK(clock.PtsToUnixMs(900'000) == kWall);
    }

    SECTION("inferred discontinuity is reported only after confirmation") {
        AcceptPcr(900'000);

        CHECK_FALSE(RecordPcr(100).has_value());

        const auto discontinuity = RecordPcr(50);
        REQUIRE(discontinuity.has_value());
        CHECK(discontinuity->pid == kPcrPid);
        CHECK_FALSE(discontinuity->flagged);
    }
}
