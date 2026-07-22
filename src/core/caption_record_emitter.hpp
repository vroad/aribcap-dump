// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

#include "aribcaption/decoder.hpp"

namespace ts {
class PESPacket;
}  // namespace ts

namespace aribcap_dump {

class OutputRecordSink;
class ProgramClock;

// Interface for ARIB caption PES decoding, letting tests substitute a fake for libaribcaption.
class CaptionDecoder {
   public:
    virtual ~CaptionDecoder() = default;
    [[nodiscard]] virtual aribcaption::DecodeStatus Decode(const std::uint8_t* data,
                                                           std::size_t size, std::int64_t pts_ms,
                                                           aribcaption::DecodeResult& result) = 0;
    [[nodiscard]] virtual std::uint32_t QueryISO6392LanguageCode(
        aribcaption::LanguageId language_id) const = 0;
};

using CaptionDecoderList = std::array<std::unique_ptr<CaptionDecoder>, 2>;
using CaptionDecoderFactory =
    std::function<std::unique_ptr<CaptionDecoder>(aribcaption::LanguageId language_id)>;

struct CaptionRecordEmitterOptions {
    bool emit_empty_captions = false;
};

// Per-PID caption record emitter: decodes one ARIB caption elementary stream's PES payloads.
//
// It always starts with the first-language decoder. When caption management data reveals a second
// language, it creates the second-language decoder and feeds both languages from then on.
class CaptionRecordEmitter {
   public:
    CaptionRecordEmitter(OutputRecordSink& sink, const ProgramClock& clock,
                         aribcaption::Profile profile, CaptionRecordEmitterOptions options = {});
    CaptionRecordEmitter(OutputRecordSink& sink, const ProgramClock& clock,
                         aribcaption::Profile profile, CaptionDecoderFactory decoder_factory,
                         CaptionRecordEmitterOptions options = {});

    [[nodiscard]] aribcaption::Profile Profile() const {
        return profile_;
    }
    void HandlePes(const ts::PESPacket& packet);

   private:
    void EnsureSecondLanguageDecoder();

    OutputRecordSink& sink_;
    const ProgramClock& clock_;
    aribcaption::Profile profile_;
    CaptionRecordEmitterOptions options_;
    CaptionDecoderList decoders_;
    CaptionDecoderFactory decoder_factory_;
};

}  // namespace aribcap_dump
