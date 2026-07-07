// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/caption_classifier.hpp"

#include <cstdint>
#include <optional>

#include "tests/pmt_test_utils.hpp"
#include "tsduck/tsStreamType.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace {

// Reuse one fixed ES PID across all tests; it never influences classification.
constexpr ts::PID kEsPid = 0x0100;

// PMT PID outside the one-seg range; the exact value doesn't matter beyond that.
constexpr ts::PID kNonOneSegPmtPid = 0x1000;

// Just outside the ARIB TR-B14 one-seg PMT PID range (0x1FC8-0x1FCF) on either side.
// Hardcoded independently of aribcap_dump::kOneSegPmtPidMin/Max so a wrong boundary
// constant in production can't validate itself against these tests.
constexpr ts::PID kOneSegPmtPidJustBelow = 0x1FC7;
constexpr ts::PID kOneSegPmtPidJustAbove = 0x1FD0;

// A component_tag with no meaning defined for caption classification.
constexpr std::uint8_t kUnknownComponentTag = 0x99;
// A data_component_id ClassifyCaptionStream recognizes but rejects for Profile A.
constexpr std::uint16_t kUnsupportedDataComponentId = 0x0013;

class CaptionClassifierFixture {
   public:
    ts::DuckContext context;
    ts::PMT pmt;

    ts::PMT::Stream& AddCaptionComponent(std::optional<std::uint8_t> component_tag,
                                         std::optional<std::uint16_t> data_component_id) {
        return aribcap_dump::test::AddPrivatePesStream(context, pmt, kEsPid, component_tag,
                                                       data_component_id);
    }

    void CheckCaptionStream(const ts::PMT::Stream& stream, ts::PID pmt_pid,
                            aribcaption::Profile expected_profile,
                            aribcaption::CaptionType expected_caption_type) {
        const auto result = aribcap_dump::ClassifyCaptionStream(context, pmt_pid, stream);
        REQUIRE(result.has_value());

        CHECK(result->profile == expected_profile);
        CHECK(result->caption_type == expected_caption_type);
    }

    void CheckRejected(const ts::PMT::Stream& stream, ts::PID pmt_pid) {
        CHECK_FALSE(aribcap_dump::ClassifyCaptionStream(context, pmt_pid, stream).has_value());
    }
};

}  // namespace

// -------------------------------------------------------------------------------------------------
// Accepted stream tests
// -------------------------------------------------------------------------------------------------

TEST_CASE_METHOD(CaptionClassifierFixture,
                 "ClassifyCaptionStream recognizes Profile A caption and superimpose streams") {
    SECTION("caption component") {
        // 0x30/0x37 hardcoded per ARIB TR-B14 v6.7 Fascicle 2, section 4.2.8.1, independent of
        // the production constants.
        const auto component_tag = GENERATE(std::uint8_t{0x30}, std::uint8_t{0x37});
        CAPTURE(component_tag);

        const auto& stream =
            AddCaptionComponent(component_tag, aribcap_dump::kDataComponentProfileA);

        CheckCaptionStream(stream, kNonOneSegPmtPid, aribcaption::Profile::kProfileA,
                           aribcaption::CaptionType::kCaption);
    }

    SECTION("superimpose component") {
        // 0x38/0x3F hardcoded per ARIB TR-B14 v6.7 Fascicle 2, section 4.2.8.1, independent of
        // the production constants.
        const auto component_tag = GENERATE(std::uint8_t{0x38}, std::uint8_t{0x3F});
        CAPTURE(component_tag);

        const auto& stream =
            AddCaptionComponent(component_tag, aribcap_dump::kDataComponentProfileA);

        CheckCaptionStream(stream, kNonOneSegPmtPid, aribcaption::Profile::kProfileA,
                           aribcaption::CaptionType::kSuperimpose);
    }

    SECTION("missing data_component_id defaults to Profile A") {
        const auto& stream =
            AddCaptionComponent(aribcap_dump::kComponentTagCaptionMin, std::nullopt);

        CheckCaptionStream(stream, kNonOneSegPmtPid, aribcaption::Profile::kProfileA,
                           aribcaption::CaptionType::kCaption);
    }
}

TEST_CASE_METHOD(CaptionClassifierFixture,
                 "ClassifyCaptionStream recognizes Profile C one-seg caption streams") {
    const auto& stream = AddCaptionComponent(aribcap_dump::kComponentTagOneSegCaption,
                                             aribcap_dump::kDataComponentProfileC);

    // 0x1FC8/0x1FCF hardcoded per ARIB TR-B14, independent of the production constants.
    const ts::PID pmt_pid = GENERATE(ts::PID{0x1FC8}, ts::PID{0x1FCF});
    CAPTURE(pmt_pid);

    CheckCaptionStream(stream, pmt_pid, aribcaption::Profile::kProfileC,
                       aribcaption::CaptionType::kCaption);
}

// -------------------------------------------------------------------------------------------------
// Rejected stream tests, ordered by validation step
// -------------------------------------------------------------------------------------------------

TEST_CASE_METHOD(CaptionClassifierFixture,
                 "ClassifyCaptionStream rejects non-private-PES stream types") {
    auto& stream = pmt.streams[0x101];
    stream.stream_type = ts::ST_MPEG2_VIDEO;

    CheckRejected(stream, kNonOneSegPmtPid);
}

TEST_CASE_METHOD(CaptionClassifierFixture,
                 "ClassifyCaptionStream rejects unsupported component descriptors") {
    SECTION("missing component tag") {
        const auto& stream =
            AddCaptionComponent(std::nullopt, aribcap_dump::kDataComponentProfileA);

        CheckRejected(stream, kNonOneSegPmtPid);
    }

    SECTION("unknown component tag") {
        // Just outside the ARIB TR-B14 v6.7 Fascicle 2, section 4.2.8.1 caption/superimpose
        // ranges (0x30-0x3F) on either side, plus one value far outside any range, hardcoded
        // independently of the production constants.
        const auto component_tag =
            GENERATE(std::uint8_t{0x2F}, std::uint8_t{0x40}, kUnknownComponentTag);
        CAPTURE(component_tag);

        const auto& stream =
            AddCaptionComponent(component_tag, aribcap_dump::kDataComponentProfileA);

        CheckRejected(stream, kNonOneSegPmtPid);
    }

    SECTION("unsupported data_component_id") {
        const auto& stream =
            AddCaptionComponent(aribcap_dump::kComponentTagCaptionMin, kUnsupportedDataComponentId);

        CheckRejected(stream, kNonOneSegPmtPid);
    }
}

TEST_CASE_METHOD(CaptionClassifierFixture,
                 "ClassifyCaptionStream rejects invalid Profile C one-seg combinations") {
    SECTION("Profile C descriptor outside the one-seg PMT PID range") {
        const auto& stream = AddCaptionComponent(aribcap_dump::kComponentTagOneSegCaption,
                                                 aribcap_dump::kDataComponentProfileC);

        const ts::PID pmt_pid =
            GENERATE(kNonOneSegPmtPid, kOneSegPmtPidJustBelow, kOneSegPmtPidJustAbove);
        CAPTURE(pmt_pid);

        CheckRejected(stream, pmt_pid);
    }

    SECTION("Profile A caption descriptor in the one-seg PMT PID range") {
        const auto& stream = AddCaptionComponent(aribcap_dump::kComponentTagCaptionMin,
                                                 aribcap_dump::kDataComponentProfileA);

        CheckRejected(stream, aribcap_dump::kOneSegPmtPidMin);
    }

    SECTION("Profile A superimpose descriptor in the one-seg PMT PID range") {
        const auto& stream = AddCaptionComponent(aribcap_dump::kComponentTagSuperimposeMin,
                                                 aribcap_dump::kDataComponentProfileA);

        CheckRejected(stream, aribcap_dump::kOneSegPmtPidMax);
    }

    SECTION("one-seg component tag without data_component_id") {
        const auto& stream =
            AddCaptionComponent(aribcap_dump::kComponentTagOneSegCaption, std::nullopt);

        CheckRejected(stream, aribcap_dump::kOneSegPmtPidMin);
    }

    SECTION("one-seg component tag with Profile A data_component_id") {
        const auto& stream = AddCaptionComponent(aribcap_dump::kComponentTagOneSegCaption,
                                                 aribcap_dump::kDataComponentProfileA);

        CheckRejected(stream, aribcap_dump::kOneSegPmtPidMin);
    }
}
