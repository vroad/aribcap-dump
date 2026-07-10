// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/ts_utils.hpp"

#include <cstdint>
#include <limits>

#include "tests/pes_test_utils.hpp"
#include "tsduck/tsPESPacket.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace {

constexpr std::uint64_t kMax33BitPts = (std::uint64_t{1} << 33) - 1;

}  // namespace

// -------------------------------------------------------------------------------------------------
// 90 kHz timestamp conversion tests
// -------------------------------------------------------------------------------------------------

TEST_CASE("Pts90kToMs converts 90kHz timestamps to integer milliseconds") {
    CHECK(aribcap_dump::Pts90kToMs(0) == 0);
    CHECK(aribcap_dump::Pts90kToMs(90'000) == 1000);
    CHECK(aribcap_dump::Pts90kToMs(180'000) == 2000);
    CHECK(aribcap_dump::Pts90kToMs(45'000) == 500);
    CHECK(aribcap_dump::Pts90kToMs(89'999) == 999);
    CHECK(aribcap_dump::Pts90kToMs(kMax33BitPts) == 95'443'717);
}

TEST_CASE("Pts90kToMs masks to the 33-bit PTS/DTS domain instead of overflowing") {
    // One past the 33-bit domain must wrap back to 0.
    CHECK(aribcap_dump::Pts90kToMs(kMax33BitPts + 1) == 0);
    // 2^64 is an exact multiple of 2^33. Masking UINT64_MAX to 33 bits therefore leaves all
    // 33 low bits set — the same value as kMax33BitPts.
    //
    // The CHECK below would fail if Pts90kToMs used a mask width other than 33 bits.
    CHECK(aribcap_dump::Pts90kToMs(std::numeric_limits<std::uint64_t>::max()) ==
          aribcap_dump::Pts90kToMs(kMax33BitPts));
}

// -------------------------------------------------------------------------------------------------
// PES PTS parsing tests
// -------------------------------------------------------------------------------------------------

TEST_CASE("ParsePesPts decodes a PTS encoded by TSDuck") {
    const std::uint64_t pts =
        GENERATE(std::uint64_t{0}, std::uint64_t{1}, std::uint64_t{90'000}, std::uint64_t{990'000},
                 std::uint64_t{1} << 32, kMax33BitPts);

    // The packet body is irrelevant; the CHECK below only verifies PTS decoding from the PES
    // header.
    const ts::PESPacket packet = aribcap_dump::test::MakePesPacket({}, pts);
    const auto decoded = aribcap_dump::ParsePesPts(packet);

    REQUIRE(decoded.has_value());
    CHECK(*decoded == pts);
}

// -------------------------------------------------------------------------------------------------
// RFC 3339 timestamp formatting tests
// -------------------------------------------------------------------------------------------------

TEST_CASE("UnixMsToRfc3339Jst formats Unix ms as JST RFC 3339 strings") {
    // Sub-second milliseconds are always printed with 3 digits.
    CHECK(aribcap_dump::UnixMsToRfc3339Jst(1'577'804'400'123) == "2020-01-01T00:00:00.123+09:00");
    // This case verifies that the offset is applied before the date is split out.
    // 2019-12-31T20:00:00Z becomes 2020-01-01T05:00:00+09:00 after applying the
    // JST offset.
    CHECK(aribcap_dump::UnixMsToRfc3339Jst(1'577'822'400'000) == "2020-01-01T05:00:00.000+09:00");
    // 1970-01-01T00:00:00Z becomes 1970-01-01T09:00:00+09:00 after applying the
    // JST offset.
    CHECK(aribcap_dump::UnixMsToRfc3339Jst(0) == "1970-01-01T09:00:00.000+09:00");
}
