// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <vector>

#include "core/output_record.hpp"
#include "tsduck/tsBinaryTable.h"
#include "tsduck/tsDuckContext.h"
#include "tsduck/tsEIT.h"

namespace aribcap_dump {

// `EitRecordEmitter::HandleEit` input, deserialized from one EIT present/following binary table.
struct DeserializedEit {
    ts::EIT eit;
    std::uint8_t version = 0;

    // Indicates whether the present section (section number 0) carried an event; the emitter uses
    // it to decide whether index 0 holds the present or the following event.
    //
    // When no event is currently airing (e.g. between scheduled programmes), the present section
    // is validly empty. An empty present section does not mean the table is malformed: because
    // `ts::EIT::events` is keyed by insertion order across all sections, the following event lands
    // at index 0 when the present section is empty.
    bool present_section_has_event = false;
};

// Deserializes `table` into a `DeserializedEit`.
[[nodiscard]] DeserializedEit DeserializeEit(ts::DuckContext& context,
                                             const ts::BinaryTable& table);

// Parses a `DeserializedEit` into at most two output records.
//
// `deserialized.present_section_has_event` determines the `EitSection` assigned to the events
// at index 0 and 1:
//   - if true:  index 0 is `kPresent`, index 1 is `kFollowing`
//   - if false: index 0 is `kFollowing` (the present section had no record)
[[nodiscard]] std::vector<EitRecord> ParseEit(ts::DuckContext& context,
                                              const DeserializedEit& deserialized);

}  // namespace aribcap_dump
