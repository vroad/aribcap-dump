// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/eit_record_emitter.hpp"

#include <cstdint>
#include <tuple>
#include <variant>
#include <vector>

#include "core/output_record.hpp"
#include "core/output_record_sink.hpp"
#include "tests/eit_test_utils.hpp"
#include "tscore/tsTime.h"
#include "tsduck/tsDuckContext.h"
#include "tsduck/tsEIT.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_adapters.hpp>

namespace {

using aribcap_dump::test::MakePfEit;

struct EitRecordEmitterFixture {
    ts::DuckContext context;
    aribcap_dump::VectorOutputRecordSink sink;
    aribcap_dump::EitRecordEmitter emitter{sink};

    // Builds a single-event present/following EIT and runs it through `HandleEit`.
    void Feed(std::uint16_t service_id, std::uint16_t event_id, std::uint8_t version) {
        auto eit = MakePfEit(service_id, version);
        auto& event = eit.events.newEntry();
        event.event_id = event_id;
        event.start_time = ts::Time(2020, 1, 1, 0, 0, 0);
        event.duration = cn::seconds(60);

        const aribcap_dump::DeserializedEit deserialized{
            .eit = eit,
            .version = version,
            .present_section_has_event = true,
        };
        emitter.HandleEit(context, deserialized);
    }
};

}  // namespace

TEST_CASE("EitRecordEmitter filters events by output mode") {
    ts::DuckContext context;
    aribcap_dump::VectorOutputRecordSink sink;
    using TestCase = std::tuple<aribcap_dump::EitOutputMode, std::vector<aribcap_dump::EitSection>>;
    const auto [output_mode, expected_sections] =
        GENERATE(table<aribcap_dump::EitOutputMode, std::vector<aribcap_dump::EitSection>>({
            TestCase{aribcap_dump::EitOutputMode::kPresent, {aribcap_dump::EitSection::kPresent}},
            TestCase{aribcap_dump::EitOutputMode::kFollowing,
                     {aribcap_dump::EitSection::kFollowing}},
            TestCase{aribcap_dump::EitOutputMode::kPresentFollowing,
                     {aribcap_dump::EitSection::kPresent, aribcap_dump::EitSection::kFollowing}},
        }));
    aribcap_dump::EitRecordEmitter emitter(sink, output_mode);
    auto eit = MakePfEit(1024, 0);

    for (std::uint16_t event_id : {0x1234, 0x1235}) {
        auto& event = eit.events.newEntry();
        event.event_id = event_id;
        event.start_time = ts::Time(2020, 1, 1, 0, 0, 0);
        event.duration = cn::seconds(60);
    }

    emitter.HandleEit(context, aribcap_dump::DeserializedEit{
                                   .eit = eit,
                                   .version = 0,
                                   .present_section_has_event = true,
                               });

    REQUIRE(sink.Records().size() == expected_sections.size());

    for (std::size_t i = 0; i < expected_sections.size(); ++i) {
        CHECK(std::get<aribcap_dump::EitRecord>(sink.Records()[i]).section == expected_sections[i]);
    }
}

TEST_CASE_METHOD(EitRecordEmitterFixture, "EitRecordEmitter emits the first EPG event it sees") {
    Feed(1024, 0x1234, /*version=*/0);

    REQUIRE(sink.Records().size() == 1);
    const auto* eit = std::get_if<aribcap_dump::EitRecord>(&sink.Records()[0]);
    REQUIRE(eit != nullptr);
    CHECK(eit->event_id == 0x1234);
}

TEST_CASE_METHOD(EitRecordEmitterFixture,
                 "EitRecordEmitter re-emits only when the version changes") {
    Feed(1024, 0x1234, /*version=*/0);

    SECTION("same version is suppressed") {
        Feed(1024, 0x1234, /*version=*/0);

        CHECK(sink.Records().size() == 1);
    }

    SECTION("different version re-emits events") {
        Feed(1024, 0x1234, /*version=*/1);

        CHECK(sink.Records().size() == 2);
    }

    SECTION("wrapping back to a previously seen version still re-emits events") {
        // Cycles through every remaining 5-bit version (1..31) and back to 0, to confirm the
        // emitter compares only against the immediately preceding version. An implementation
        // that instead remembers every version ever seen treats this final version=0 as a
        // repeat of the `Feed` call above and wrongly drops it.
        for (std::uint8_t version = 1; version <= 31; ++version) {
            Feed(1024, 0x1234, version);
        }
        Feed(1024, 0x1234, /*version=*/0);

        CHECK(sink.Records().size() == 33);
    }
}
