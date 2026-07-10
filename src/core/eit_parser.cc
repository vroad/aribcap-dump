// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/eit_parser.hpp"

#include <algorithm>
#include <cassert>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "core/ts_utils.hpp"
#include "tsduck/tsContentDescriptor.h"
#include "tsduck/tsDID.h"
#include "tsduck/tsDescriptor.h"
#include "tsduck/tsDescriptorList.h"
#include "tsduck/tsEIT.h"
#include "tsduck/tsExtendedEventDescriptor.h"
#include "tsduck/tsShortEventDescriptor.h"

namespace aribcap_dump {
namespace {

// One description/value pair from an `extended_event_descriptor` item, before joining
// into `ParsedEventText::extended_text`.
struct ParsedExtendedEventItem {
    std::string description;
    std::string value;
};

// Descriptor strings gathered by `ParseDescriptors()` before they are copied into the
// public `EitRecord` struct, which is the actual JSONL output type.
struct ParsedEventText {
    // Genre nibbles, one `EitGenre` per `content_descriptor` entry (a single `content_descriptor`
    // can carry several); ordered by descriptor position, then by entry position within each
    // descriptor.
    std::vector<EitGenre> genres;
    // UTF-8 decoded `short_event_descriptor`s, kept in descriptor-list order.
    std::vector<EitShortEvent> short_events;
    // UTF-8 decoded text from the `extended_event_descriptor`'s item list and trailing text,
    // joined into a single string by `FormatExtendedText()`.
    std::string extended_text;
};

[[nodiscard]] EitSection GetSectionKind(std::size_t event_index, bool present_section_has_event) {
    if (present_section_has_event && event_index == 0) {
        return EitSection::kPresent;
    }

    return EitSection::kFollowing;
}

void PushParsedExtendedEventItem(std::vector<ParsedExtendedEventItem>* items,
                                 std::string description, std::string value) {
    // A missing description signals a continuation of the previous entry's value; ARIB STD-B10
    // splits long values across multiple entries at descriptor size boundaries. Append this
    // entry's value onto the last item instead of pushing a new one.
    if (description.empty() && !items->empty()) {
        items->back().value += value;

        return;
    }

    items->push_back(ParsedExtendedEventItem{
        .description = std::move(description),
        .value = std::move(value),
    });
}

// Formats extended event items and trailing free text into a single newline-joined string.
[[nodiscard]] std::string FormatExtendedText(const std::vector<ParsedExtendedEventItem>& items,
                                             const std::string& trailing_text) {
    std::string out;

    auto append_part = [&out](const std::string& part) {
        if (!out.empty()) {
            out += '\n';
        }

        out += part;
    };

    for (const auto& item : items) {
        if (item.description.empty()) {
            // Unnamed items render as bare value.
            if (!item.value.empty()) {
                append_part(item.value);
            }

        } else {
            // Keyed items render as "description\nvalue".
            append_part(item.description + "\n" + item.value);
        }
    }

    if (!trailing_text.empty()) {
        append_part(trailing_text);
    }

    return out;
}

[[nodiscard]] ParsedEventText ParseDescriptors(ts::DuckContext& context,
                                               const ts::DescriptorList& descriptors) {
    ParsedEventText out;
    std::vector<ParsedExtendedEventItem> items;
    // The `extended_event_descriptor`'s free-text field, separate from its item list (ARIB
    // STD-B10, 6.2.7). Accumulate the same way as for items: append across descriptor
    // instances when a logical extended event is split via
    // descriptor_number/last_descriptor_number.
    std::string trailing_text;

    for (auto index = descriptors.search(ts::DID_DVB_SHORT_EVENT); index < descriptors.count();
         index = descriptors.search(ts::DID_DVB_SHORT_EVENT, index + 1)) {
        const auto& descriptor = descriptors[index];

        const ts::ShortEventDescriptor short_event(context, descriptor);

        if (short_event.isValid()) {
            out.short_events.push_back(EitShortEvent{
                .event_name = short_event.event_name.toUTF8(),
                .text = short_event.text.toUTF8(),
                .language_code = short_event.language_code.toUTF8(),
            });
        }
    }

    for (auto index = descriptors.search(ts::DID_DVB_CONTENT); index < descriptors.count();
         index = descriptors.search(ts::DID_DVB_CONTENT, index + 1)) {
        const auto& descriptor = descriptors[index];

        const ts::ContentDescriptor content(context, descriptor);

        if (content.isValid()) {
            for (const auto& entry : content.entries) {
                out.genres.push_back(EitGenre{
                    .content_nibble_level_1 = entry.content_nibble_level_1,
                    .content_nibble_level_2 = entry.content_nibble_level_2,
                    .user_nibble_1 = entry.user_nibble_1,
                    .user_nibble_2 = entry.user_nibble_2,
                });
            }
        }
    }

    for (auto index = descriptors.search(ts::DID_DVB_EXTENDED_EVENT); index < descriptors.count();
         index = descriptors.search(ts::DID_DVB_EXTENDED_EVENT, index + 1)) {
        const auto& descriptor = descriptors[index];

        const ts::ExtendedEventDescriptor extended_event(context, descriptor);

        if (extended_event.isValid()) {
            for (const auto& entry : extended_event.entries) {
                PushParsedExtendedEventItem(&items, entry.item_description.toUTF8(),
                                            entry.item.toUTF8());
            }

            trailing_text += extended_event.text.toUTF8();
        }
    }

    out.extended_text = FormatExtendedText(items, trailing_text);

    return out;
}

// Checks whether the EIT present section (section number 0) of `table` carries an event.
// See `DeserializedEit::present_section_has_event` for why this matters.
[[nodiscard]] bool EitPresentSectionHasEvent(const ts::BinaryTable& table) {
    const auto& present_section = table.sectionAt(0);

    return present_section != nullptr && present_section->isValid() &&
           present_section->payloadSize() > ts::EIT::EIT_PAYLOAD_FIXED_SIZE;
}

}  // namespace

DeserializedEit DeserializeEit(ts::DuckContext& context, const ts::BinaryTable& table) {
    return DeserializedEit{
        .eit = ts::EIT(context, table),
        .version = table.version(),
        .present_section_has_event = EitPresentSectionHasEvent(table),
    };
}

std::vector<EitRecord> ParseEit(ts::DuckContext& context, const DeserializedEit& deserialized) {
    const ts::EIT& eit = deserialized.eit;

    // Callers must deliver only "current" (not next), present/following,
    // actual EITs. The asserts catch contract violations in debug builds only.
    assert(eit.isCurrent() && "ParseEit requires a current EIT");
    assert(eit.isActual() && "ParseEit requires an actual-stream EIT");
    assert(eit.isPresentFollowing() && "ParseEit requires a present/following EIT");

    std::vector<EitRecord> results;

    if (!eit.isValid() || eit.events.empty()) {
        return results;
    }

    results.reserve(std::min<std::size_t>(eit.events.size(), 2));

    for (const auto& [_, event] : eit.events) {
        if (results.size() >= 2) {
            break;
        }

        EitRecord parsed;
        parsed.start_time_ms = JstTimeToUnixMs(event.start_time);

        if (event.duration.count() >= 0 &&
            event.duration.count() <= std::numeric_limits<std::uint32_t>::max()) {
            parsed.duration_sec = static_cast<std::uint32_t>(event.duration.count());
        }

        auto event_text = ParseDescriptors(context, event.descs);
        parsed.short_events = std::move(event_text.short_events);
        parsed.extended_text = std::move(event_text.extended_text);
        parsed.version = deserialized.version;
        parsed.service_id = eit.service_id;
        parsed.transport_stream_id = eit.ts_id;
        parsed.original_network_id = eit.onetw_id;
        parsed.section = GetSectionKind(results.size(), deserialized.present_section_has_event);
        parsed.event_id = event.event_id;
        parsed.genres = std::move(event_text.genres);
        results.push_back(std::move(parsed));
    }

    return results;
}

}  // namespace aribcap_dump
