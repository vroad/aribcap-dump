// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/output_record.hpp"

#include <catch2/catch_test_macros.hpp>

// -------------------------------------------------------------------------------------------------
// Caption JSON tests
// -------------------------------------------------------------------------------------------------

TEST_CASE("caption serializes in JSONL shape") {
    const aribcap_dump::OutputRecord record = aribcap_dump::CaptionRecord{
        .pid = 2,
        .language_code = "jpn",
        .time_ms = 1'577'804'400'000,
        .duration_ms = 5'000,
        .clear_screen = true,
        .text = "caption",
    };
    CHECK(
        aribcap_dump::ToJsonLine(record) ==
        R"({"type":"caption","pid":2,"captionType":"caption","languageCode":"jpn","timeMs":1577804400000,"durationMs":5000,"clearScreen":true,"color":null,"text":"caption","ruby":[]})");
}

TEST_CASE("caption null time serializes as null") {
    const aribcap_dump::OutputRecord record = aribcap_dump::CaptionRecord{
        .pid = 2,
        .language_code = "jpn",
        .time_ms = std::nullopt,
        .text = "caption",
    };
    CHECK(
        aribcap_dump::ToJsonLine(record) ==
        R"({"type":"caption","pid":2,"captionType":"caption","languageCode":"jpn","timeMs":null,"durationMs":null,"clearScreen":false,"color":null,"text":"caption","ruby":[]})");
}

TEST_CASE("caption color serializes as JSON string") {
    const aribcap_dump::OutputRecord record = aribcap_dump::CaptionRecord{
        .pid = 2,
        .language_code = "jpn",
        .time_ms = std::nullopt,
        .color = "0xffff00ff",
        .text = "caption",
    };
    CHECK(
        aribcap_dump::ToJsonLine(record) ==
        R"({"type":"caption","pid":2,"captionType":"caption","languageCode":"jpn","timeMs":null,"durationMs":null,"clearScreen":false,"color":"0xffff00ff","text":"caption","ruby":[]})");
}

TEST_CASE("caption ruby serializes as string array") {
    const aribcap_dump::OutputRecord record = aribcap_dump::CaptionRecord{
        .pid = 2,
        .language_code = "jpn",
        .time_ms = std::nullopt,
        .color = "0xffffffff",
        .text = "明日は晴れです",
        .ruby = {"あした", "は"},
    };
    CHECK(
        aribcap_dump::ToJsonLine(record) ==
        R"({"type":"caption","pid":2,"captionType":"caption","languageCode":"jpn","timeMs":null,"durationMs":null,"clearScreen":false,"color":"0xffffffff","text":"明日は晴れです","ruby":["あした","は"]})");
}

TEST_CASE("caption type serializes superimpose") {
    const aribcap_dump::OutputRecord record = aribcap_dump::CaptionRecord{
        .pid = 2,
        .caption_type = aribcap_dump::CaptionRecordType::kSuperimpose,
        .language_code = "jpn",
        .time_ms = std::nullopt,
        .text = "caption",
    };
    CHECK(
        aribcap_dump::ToJsonLine(record) ==
        R"({"type":"caption","pid":2,"captionType":"superimpose","languageCode":"jpn","timeMs":null,"durationMs":null,"clearScreen":false,"color":null,"text":"caption","ruby":[]})");
}

// -------------------------------------------------------------------------------------------------
// EIT JSON tests
// -------------------------------------------------------------------------------------------------

TEST_CASE("EIT serializes unit-suffixed camelCase fields") {
    const aribcap_dump::OutputRecord record = aribcap_dump::EitRecord{
        .version = 5,
        .service_id = 18432,
        .transport_stream_id = 12345,
        .original_network_id = 32736,
        .event_id = 10,
        .section = aribcap_dump::EitSection::kFollowing,
        .short_events = {{
            "jpn",
            "name",
            "text",
        }},
        .extended_text = "extended",
        .start_time_ms = 1'577'804'400'000,
        .duration_sec = 1800,
    };
    CHECK(aribcap_dump::ToJsonLine(record) ==
          R"({"type":"eit","version":5,"serviceId":18432,"transportStreamId":12345)"
          R"(,"originalNetworkId":32736,"eventId":10,"section":"following","shortEvents":[)"
          R"({"languageCode":"jpn","eventName":"name","text":"text"}])"
          R"(,"extendedText":"extended","startTimeMs":1577804400000,"durationSec":1800})");
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
