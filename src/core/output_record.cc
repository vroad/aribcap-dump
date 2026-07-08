// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/output_record.hpp"

#include <jsoncons/json.hpp>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace aribcap_dump {
namespace {

using Json = jsoncons::ojson;

template <typename T>
void AddOptional(Json* object, const std::string& key, const std::optional<T>& value) {
    if (value.has_value()) {
        (*object)[key] = *value;
    } else {
        (*object)[key] = jsoncons::null_type();
    }
}

[[nodiscard]] Json DiagnosticToJson(const DiagnosticRecord& diagnostic) {
    Json out;
    out["type"] = "diagnostic";
    std::visit(
        [&](const auto& kind) {
            using Kind = std::decay_t<decltype(kind)>;

            if constexpr (std::is_same_v<Kind, CaptionDecodeError>) {
                out["kind"] = "captionDecodeError";
                out["pid"] = kind.pid;
            } else if constexpr (std::is_same_v<Kind, ReservedPmtPidCollision>) {
                out["kind"] = "reservedPmtPidCollision";
                out["pid"] = kind.pid;
            } else if constexpr (std::is_same_v<Kind, PcrDiscontinuity>) {
                out["kind"] = "pcrDiscontinuity";
                out["pid"] = kind.pid;
                out["flagged"] = kind.flagged;
            }
        },
        diagnostic.kind);

    return out;
}

[[nodiscard]] Json ShortEventsToJson(const std::vector<EitShortEvent>& short_events) {
    Json out = Json::array();
    out.reserve(short_events.size());

    for (const auto& short_event : short_events) {
        Json value;
        value["languageCode"] = short_event.language_code;
        value["eventName"] = short_event.event_name;
        value["text"] = short_event.text;
        out.push_back(std::move(value));
    }

    return out;
}

[[nodiscard]] Json GenresToJson(const std::vector<EitGenre>& genres) {
    Json out = Json::array();
    out.reserve(genres.size());

    for (const auto& genre : genres) {
        Json value;
        value["contentNibbleLevel1"] = genre.content_nibble_level_1;
        value["contentNibbleLevel2"] = genre.content_nibble_level_2;
        value["userNibble1"] = genre.user_nibble_1;
        value["userNibble2"] = genre.user_nibble_2;
        out.push_back(std::move(value));
    }

    return out;
}

[[nodiscard]] Json RubyToJson(const std::vector<std::string>& ruby) {
    Json out = Json::array();
    out.reserve(ruby.size());

    for (const auto& ruby_text : ruby) {
        out.push_back(ruby_text);
    }

    return out;
}

}  // namespace

const char* ToString(CaptionRecordType caption_type) {
    switch (caption_type) {
        case CaptionRecordType::kCaption:
            return "caption";
        case CaptionRecordType::kSuperimpose:
            return "superimpose";
    }

    return "caption";
}

const char* ToString(EitSection section) {
    switch (section) {
        case EitSection::kPresent:
            return "present";
        case EitSection::kFollowing:
            return "following";
    }

    return "present";
}

std::string ToJsonLine(const OutputRecord& record) {
    Json out;
    std::visit(
        [&](const auto& value) {
            using Value = std::decay_t<decltype(value)>;

            if constexpr (std::is_same_v<Value, CaptionRecord>) {
                out["type"] = "caption";
                out["pid"] = value.pid;
                out["captionType"] = ToString(value.caption_type);
                AddOptional(&out, "languageCode", value.language_code);
                AddOptional(&out, "timeMs", value.time_ms);
                AddOptional(&out, "durationMs", value.duration_ms);
                out["clearScreen"] = value.clear_screen;
                AddOptional(&out, "color", value.color);
                out["text"] = value.text;
                out["ruby"] = RubyToJson(value.ruby);
            } else if constexpr (std::is_same_v<Value, EitRecord>) {
                out["type"] = "eit";
                out["version"] = value.version;
                out["serviceId"] = value.service_id;
                out["transportStreamId"] = value.transport_stream_id;
                out["originalNetworkId"] = value.original_network_id;
                out["eventId"] = value.event_id;
                out["section"] = ToString(value.section);
                out["genres"] = GenresToJson(value.genres);
                out["shortEvents"] = ShortEventsToJson(value.short_events);
                out["extendedText"] = value.extended_text;
                AddOptional(&out, "startTimeMs", value.start_time_ms);
                AddOptional(&out, "durationSec", value.duration_sec);
            } else if constexpr (std::is_same_v<Value, DiagnosticRecord>) {
                out = DiagnosticToJson(value);
            }
        },
        record);

    return out.to_string();
}

}  // namespace aribcap_dump
