// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace aribcap_dump {

// -------------------------------------------------------------------------------------------------
// Caption record types
// -------------------------------------------------------------------------------------------------

enum class CaptionRecordType {
    kCaption,
    kSuperimpose,
};

// One decoded ARIB caption line ready for JSONL output.
struct CaptionRecord {
    std::uint16_t pid = 0;
    CaptionRecordType caption_type = CaptionRecordType::kCaption;
    std::optional<std::string> language_code;
    std::optional<std::int64_t> time_ms;
    // Presentation duration in milliseconds; absent when the caption's end time is indefinite
    // (presented until the next caption's PTS).
    std::optional<std::int64_t> duration_ms;
    // Set when libaribcaption's `kCaptionFlagsClearScreen` flag is present, i.e. the screen
    // should be cleared before this caption is presented.
    bool clear_screen = false;
    std::optional<std::string> color;
    std::string text;
    std::vector<std::string> ruby;
};

// -------------------------------------------------------------------------------------------------
// EIT record types
// -------------------------------------------------------------------------------------------------

enum class EitSection {
    kPresent,
    kFollowing,
};

// Language, event name, and short description from one `short_event_descriptor`.
struct EitShortEvent {
    std::string language_code;
    std::string event_name;
    std::string text;
};

// Raw nibble values from one `content_descriptor` entry (ARIB STD-B10 / ETSI EN 300 468,
// 6.2.9). Left undecoded: translating nibbles to genre names is a downstream consumer's job,
// not this tool's.
struct EitGenre {
    std::uint8_t content_nibble_level_1 = 0;
    std::uint8_t content_nibble_level_2 = 0;
    std::uint8_t user_nibble_1 = 0;
    std::uint8_t user_nibble_2 = 0;
};

// One parsed EPG event (present or following section) ready for JSONL output.
struct EitRecord {
    // Version of the EIT (sub)table this event came from, letting consumers tell a revised
    // event record from a brand-new one.
    std::uint8_t version = 0;
    std::uint16_t service_id = 0;
    std::uint16_t transport_stream_id = 0;
    std::uint16_t original_network_id = 0;
    std::uint16_t event_id = 0;
    EitSection section = EitSection::kPresent;
    std::vector<EitShortEvent> short_events;
    // Raw genre nibbles from `content_descriptor` entries, kept in descriptor-list order.
    std::vector<EitGenre> genres;
    std::string extended_text;
    std::optional<std::int64_t> start_time_ms;
    std::optional<std::uint32_t> duration_sec;
};

// -------------------------------------------------------------------------------------------------
// Diagnostic record types
// -------------------------------------------------------------------------------------------------

// Caption PES decode failure on `pid`.
struct CaptionDecodeError {
    std::uint16_t pid = 0;
};

// PCR clock re-sync on `pid`.
struct PcrDiscontinuity {
    std::uint16_t pid = 0;
    // Distinguishes an expected discontinuity_indicator from an inferred (unflagged) jump.
    bool flagged = false;
};

// PMT PID collision: the target service's PMT PID (`pid`) is already reserved for
// PAT/EIT/TDT demuxing.
struct ReservedPmtPidCollision {
    std::uint16_t pid = 0;
};

using DiagnosticRecordKind =
    std::variant<CaptionDecodeError, PcrDiscontinuity, ReservedPmtPidCollision>;

// Tagged diagnostic record; `kind` identifies which condition occurred.
struct DiagnosticRecord {
    DiagnosticRecordKind kind;
};

using OutputRecord = std::variant<CaptionRecord, EitRecord, DiagnosticRecord>;

[[nodiscard]] std::string ToJsonLine(const OutputRecord& record);
[[nodiscard]] const char* ToString(CaptionRecordType caption_type);
[[nodiscard]] const char* ToString(EitSection section);

}  // namespace aribcap_dump
