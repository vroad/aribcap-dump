// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <optional>

#include "core/eit_parser.hpp"
#include "tsduck/tsDuckContext.h"

namespace aribcap_dump {

class OutputRecordSink;

enum class EitOutputMode {
    kPresent,
    kFollowing,
    kPresentFollowing,
};

// Per-service EIT record emitter: emits `EitRecord`s for the EIT sections selected by
// `output_mode`; events in unselected sections are suppressed. Re-emits only when the
// incoming EIT's version differs from the last-emitted version.
//
// It assumes the caller passes only EITs for the target service; `CaptionDumper` filters by
// service ID, and this class does not re-check.
class EitRecordEmitter {
   public:
    explicit EitRecordEmitter(OutputRecordSink& sink,
                              EitOutputMode output_mode = EitOutputMode::kPresent);

    void HandleEit(ts::DuckContext& context, const DeserializedEit& deserialized);

   private:
    OutputRecordSink& sink_;
    EitOutputMode output_mode_;
    std::optional<std::uint8_t> last_emitted_version_;
};

}  // namespace aribcap_dump
