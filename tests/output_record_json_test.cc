// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/output_record.hpp"

#include <catch2/catch_test_macros.hpp>

// -------------------------------------------------------------------------------------------------
// Caption JSON tests
// -------------------------------------------------------------------------------------------------

TEST_CASE("caption serializes in JSONL shape") {
    const aribcap_dump::OutputRecord record = aribcap_dump::CaptionRecord{
        .time_ms = 1'577'804'400'000,
        .text = "caption",
        .pid = 2,
        .language_code = "jpn",
        .duration_ms = 5'000,
        .clear_screen = true,
    };
    CHECK(
        aribcap_dump::ToJsonLine(record) ==
        R"({"type":"caption","time":"2020-01-01T00:00:00.000+09:00","text":"caption","ruby":[],"color":null,"pid":2,"captionType":"caption","languageCode":"jpn","durationMs":5000,"clearScreen":true})");
}

TEST_CASE("caption null time serializes as null") {
    const aribcap_dump::OutputRecord record = aribcap_dump::CaptionRecord{
        .time_ms = std::nullopt,
        .text = "caption",
        .pid = 2,
        .language_code = "jpn",
    };
    CHECK(
        aribcap_dump::ToJsonLine(record) ==
        R"({"type":"caption","time":null,"text":"caption","ruby":[],"color":null,"pid":2,"captionType":"caption","languageCode":"jpn","durationMs":null,"clearScreen":false})");
}

TEST_CASE("caption color serializes as JSON string") {
    const aribcap_dump::OutputRecord record = aribcap_dump::CaptionRecord{
        .time_ms = std::nullopt,
        .text = "caption",
        .color = "0xffff00ff",
        .pid = 2,
        .language_code = "jpn",
    };
    CHECK(
        aribcap_dump::ToJsonLine(record) ==
        R"({"type":"caption","time":null,"text":"caption","ruby":[],"color":"0xffff00ff","pid":2,"captionType":"caption","languageCode":"jpn","durationMs":null,"clearScreen":false})");
}

TEST_CASE("caption ruby serializes as string array") {
    const aribcap_dump::OutputRecord record = aribcap_dump::CaptionRecord{
        .time_ms = std::nullopt,
        .text = "明日は晴れです",
        .ruby = {"あした", "は"},
        .color = "0xffffffff",
        .pid = 2,
        .language_code = "jpn",
    };
    CHECK(
        aribcap_dump::ToJsonLine(record) ==
        R"({"type":"caption","time":null,"text":"明日は晴れです","ruby":["あした","は"],"color":"0xffffffff","pid":2,"captionType":"caption","languageCode":"jpn","durationMs":null,"clearScreen":false})");
}

TEST_CASE("caption type serializes superimpose") {
    const aribcap_dump::OutputRecord record = aribcap_dump::CaptionRecord{
        .time_ms = std::nullopt,
        .text = "caption",
        .pid = 2,
        .caption_type = aribcap_dump::CaptionRecordType::kSuperimpose,
        .language_code = "jpn",
    };
    CHECK(
        aribcap_dump::ToJsonLine(record) ==
        R"({"type":"caption","time":null,"text":"caption","ruby":[],"color":null,"pid":2,"captionType":"superimpose","languageCode":"jpn","durationMs":null,"clearScreen":false})");
}

// -------------------------------------------------------------------------------------------------
// EIT JSON tests
// -------------------------------------------------------------------------------------------------

TEST_CASE("EIT serializes unit-suffixed camelCase fields") {
    const aribcap_dump::OutputRecord record = aribcap_dump::EitRecord{
        .start_time_ms = 1'577'804'400'000,
        .duration_sec = 1800,
        .short_events = {{
            .event_name = "name",
            .text = "text",
            .language_code = "jpn",
        }},
        .extended_text = "extended",
        .genres = {{
            .content_nibble_level_1 = 7,
            .content_nibble_level_2 = 1,
            .user_nibble_1 = 15,
            .user_nibble_2 = 15,
        }},
        .section = aribcap_dump::EitSection::kFollowing,
        .version = 5,
        .service_id = 18432,
        .transport_stream_id = 12345,
        .original_network_id = 32736,
        .event_id = 10,
    };
    CHECK(aribcap_dump::ToJsonLine(record) ==
          R"({"type":"eit","startTime":"2020-01-01T00:00:00.000+09:00","durationSec":1800)"
          R"(,"shortEvents":[{"eventName":"name","text":"text","languageCode":"jpn"}])"
          R"(,"extendedText":"extended","genres":[)"
          R"({"contentNibbleLevel1":7,"contentNibbleLevel2":1,"userNibble1":15,"userNibble2":15}])"
          R"(,"section":"following","version":5,"serviceId":18432,"transportStreamId":12345)"
          R"(,"originalNetworkId":32736,"eventId":10})");
}

TEST_CASE("EIT null start time serializes as null") {
    const aribcap_dump::OutputRecord record = aribcap_dump::EitRecord{};

    CHECK(
        aribcap_dump::ToJsonLine(record) ==
        R"({"type":"eit","startTime":null,"durationSec":null,"shortEvents":[],"extendedText":"","genres":[],"section":"present","version":0,"serviceId":0,"transportStreamId":0,"originalNetworkId":0,"eventId":0})");
}

// -------------------------------------------------------------------------------------------------
// DiagnosticRecord JSON tests
// -------------------------------------------------------------------------------------------------

TEST_CASE("diagnostic kind is flattened") {
    const aribcap_dump::OutputRecord record = aribcap_dump::DiagnosticRecord{
        aribcap_dump::CaptionDecodeError{291},
    };
    CHECK(aribcap_dump::ToJsonLine(record) ==
          R"({"type":"diagnostic","kind":"captionDecodeError","pid":291})");
}

TEST_CASE("reserved PMT PID collision diagnostic serializes in JSONL shape") {
    const aribcap_dump::OutputRecord record = aribcap_dump::DiagnosticRecord{
        aribcap_dump::ReservedPmtPidCollision{18},
    };
    CHECK(aribcap_dump::ToJsonLine(record) ==
          R"({"type":"diagnostic","kind":"reservedPmtPidCollision","pid":18})");
}

TEST_CASE("PCR discontinuity diagnostic serializes with its flagged marker") {
    const aribcap_dump::OutputRecord record = aribcap_dump::DiagnosticRecord{
        aribcap_dump::PcrDiscontinuity{256, true},
    };
    CHECK(aribcap_dump::ToJsonLine(record) ==
          R"({"type":"diagnostic","kind":"pcrDiscontinuity","pid":256,"flagged":true})");
}
