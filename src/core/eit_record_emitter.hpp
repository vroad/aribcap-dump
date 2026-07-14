// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <map>

#include "core/eit_parser.hpp"
#include "core/output_record.hpp"
#include "tsduck/tsDuckContext.h"

namespace aribcap_dump {

class OutputRecordSink;

enum class EitOutputMode {
    kPresent,
    kFollowing,
    kPresentFollowing,
};

// Per-service EIT record emitter: emits one `EitRecord` per new EPG event in the EIT sections
// selected by `output_mode`; events in unselected sections are suppressed.
// For each `(section, event_id)`, it emits again only when the event's version differs from
// the last-emitted version for that key.
//
// It assumes the caller passes only EITs for the target service; `CaptionDumper` filters by
// service ID, and this class does not re-check.
class EitRecordEmitter {
   public:
    explicit EitRecordEmitter(OutputRecordSink& sink,
                              EitOutputMode output_mode = EitOutputMode::kPresent);

    void HandleEit(ts::DuckContext& context, const DeserializedEit& deserialized);

   private:
    // Map key for `last_emitted_version_`, identifying one EPG event by section and event ID.
    struct EitEventKey {
        EitSection section = EitSection::kPresent;
        std::uint16_t event_id = 0;

        [[nodiscard]] bool operator<(const EitEventKey& rhs) const;
    };

    OutputRecordSink& sink_;
    EitOutputMode output_mode_;
    std::map<EitEventKey, std::uint8_t> last_emitted_version_;
};

}  // namespace aribcap_dump
