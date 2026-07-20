// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/caption_record_emitter.hpp"

#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/output_record.hpp"
#include "core/output_record_sink.hpp"
#include "core/program_clock.hpp"
#include "core/ts_utils.hpp"
#include "tsduck/tsPESPacket.h"

namespace aribcap_dump {
namespace {

constexpr std::array kCaptionLanguageIds{
    aribcaption::LanguageId::kFirst,
    aribcaption::LanguageId::kSecond,
};
constexpr std::size_t kFirstLanguageIndex = 0;
constexpr std::size_t kSecondLanguageIndex = 1;

struct CaptionMetadata {
    std::vector<std::string> ruby;
    std::optional<std::string> color;
};

// Production implementation of `CaptionDecoder` that decodes through libaribcaption.
class AribCaptionDecoder final : public CaptionDecoder {
   public:
    AribCaptionDecoder(const CaptionStreamInfo& info, aribcaption::LanguageId language_id)
        : decoder_(arib_context_) {
        (void)decoder_.Initialize(aribcaption::EncodingScheme::kARIB_STD_B24_JIS, info.caption_type,
                                  info.profile, language_id);
    }

    [[nodiscard]] aribcaption::DecodeStatus Decode(const std::uint8_t* data, std::size_t size,
                                                   std::int64_t pts_ms,
                                                   aribcaption::DecodeResult& result) override {
        return decoder_.Decode(data, size, pts_ms, result);
    }

    [[nodiscard]] std::uint32_t QueryISO6392LanguageCode(
        aribcaption::LanguageId language_id) const override {
        return decoder_.QueryISO6392LanguageCode(language_id);
    }

   private:
    aribcaption::Context arib_context_;
    aribcaption::Decoder decoder_;
};

[[nodiscard]] CaptionDecoderFactory MakeAribCaptionDecoderFactory(CaptionStreamInfo info) {
    return [info](aribcaption::LanguageId language_id) {
        return std::make_unique<AribCaptionDecoder>(info, language_id);
    };
}

[[nodiscard]] std::string ColorToRgbaHex(aribcaption::ColorRGBA color) {
    const auto r = static_cast<std::uint32_t>(color.r);
    const auto g = static_cast<std::uint32_t>(color.g);
    const auto b = static_cast<std::uint32_t>(color.b);
    const auto a = static_cast<std::uint32_t>(color.a);

    const std::uint32_t rgba = (r << 24) | (g << 16) | (b << 8) | a;

    return std::format("0x{:08x}", rgba);
}

[[nodiscard]] bool HasUtf8Text(const aribcaption::CaptionChar& ch) {
    return ch.u8str[0] != '\0';
}

[[nodiscard]] std::optional<std::string> LanguageCodeToString(std::uint32_t iso6392_language_code) {
    if (iso6392_language_code == 0) {
        return std::nullopt;
    }

    std::string language_code;
    language_code.reserve(3);
    language_code.push_back(static_cast<char>((iso6392_language_code >> 16) & 0xFF));
    language_code.push_back(static_cast<char>((iso6392_language_code >> 8) & 0xFF));
    language_code.push_back(static_cast<char>(iso6392_language_code & 0xFF));

    return language_code;
}

[[nodiscard]] CaptionRecordType ToCaptionRecordType(aribcaption::CaptionType caption_type) {
    switch (caption_type) {
        case aribcaption::CaptionType::kCaption:
            return CaptionRecordType::kCaption;
        case aribcaption::CaptionType::kSuperimpose:
            return CaptionRecordType::kSuperimpose;
    }

    return CaptionRecordType::kCaption;
}

[[nodiscard]] CaptionMetadata ExtractCaptionMetadata(const aribcaption::Caption& caption) {
    CaptionMetadata metadata;

    for (const auto& region : caption.regions) {
        if (region.is_ruby) {
            std::string ruby_text;
            ruby_text.reserve(region.chars.size() * (sizeof(aribcaption::CaptionChar{}.u8str) - 1));

            for (const auto& ch : region.chars) {
                if (!HasUtf8Text(ch)) {
                    continue;
                }

                ruby_text += ch.u8str;
            }

            if (!ruby_text.empty()) {
                metadata.ruby.push_back(std::move(ruby_text));
            }

            continue;
        }

        if (!metadata.color.has_value()) {
            if (!region.chars.empty()) {
                metadata.color = ColorToRgbaHex(region.chars.front().text_color);
            }
        }
    }

    return metadata;
}

}  // namespace

CaptionRecordEmitter::CaptionRecordEmitter(OutputRecordSink& sink, const ProgramClock& clock,
                                           CaptionStreamInfo info,
                                           CaptionRecordEmitterOptions options)
    : CaptionRecordEmitter(sink, clock, info, MakeAribCaptionDecoderFactory(info), options) {}

CaptionRecordEmitter::CaptionRecordEmitter(OutputRecordSink& sink, const ProgramClock& clock,
                                           CaptionStreamInfo info,
                                           CaptionDecoderFactory decoder_factory,
                                           CaptionRecordEmitterOptions options)
    : sink_(sink),
      clock_(clock),
      info_(info),
      options_(options),
      decoders_(),
      decoder_factory_(std::move(decoder_factory)) {
    decoders_[kFirstLanguageIndex] = decoder_factory_(kCaptionLanguageIds[kFirstLanguageIndex]);
}

void CaptionRecordEmitter::EnsureSecondLanguageDecoder() {
    if (decoders_[kSecondLanguageIndex] != nullptr || !decoder_factory_) {
        return;
    }

    const auto second_language_code = decoders_[kFirstLanguageIndex]->QueryISO6392LanguageCode(
        kCaptionLanguageIds[kSecondLanguageIndex]);

    if (second_language_code != 0) {
        decoders_[kSecondLanguageIndex] =
            decoder_factory_(kCaptionLanguageIds[kSecondLanguageIndex]);
    }
}

void CaptionRecordEmitter::HandlePes(const ts::PESPacket& packet) {
    const auto pid = packet.sourcePID();
    const auto pts = ParsePesPts(packet);
    const auto time = pts.has_value() ? clock_.PtsToUnixMs(*pts) : std::nullopt;
    const auto pts_ms =
        pts.has_value() ? Pts90kToMs(*pts) : std::numeric_limits<std::int64_t>::min();

    for (auto& decoder : decoders_) {
        if (decoder == nullptr) {
            continue;
        }

        aribcaption::DecodeResult result;
        const auto status = decoder->Decode(packet.payload(), packet.payloadSize(), pts_ms, result);

        if (status == aribcaption::DecodeStatus::kError) {
            sink_.Emit(DiagnosticRecord{CaptionDecodeError{static_cast<std::uint16_t>(pid)}});

            return;
        }

        if (decoder == decoders_[kFirstLanguageIndex]) {
            EnsureSecondLanguageDecoder();
        }

        if (status != aribcaption::DecodeStatus::kGotCaption) {
            continue;
        }

        auto text = result.caption->text;
        auto metadata = ExtractCaptionMetadata(*result.caption);

        if (text.empty() && !options_.emit_empty_captions) {
            continue;
        }

        const auto duration_ms = result.caption->wait_duration == aribcaption::DURATION_INDEFINITE
                                     ? std::nullopt
                                     : std::optional<std::int64_t>(result.caption->wait_duration);

        sink_.Emit(CaptionRecord{
            .time_ms = time,
            .text = std::move(text),
            .ruby = std::move(metadata.ruby),
            .color = std::move(metadata.color),
            .pid = static_cast<std::uint16_t>(pid),
            .caption_type = ToCaptionRecordType(info_.caption_type),
            .language_code = LanguageCodeToString(result.caption->iso6392_language_code),
            .duration_ms = duration_ms,
        });
    }
}

}  // namespace aribcap_dump
