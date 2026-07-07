// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/caption_record_emitter.hpp"

#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "aribcaption/caption.hpp"
#include "core/output_record.hpp"
#include "core/output_record_sink.hpp"
#include "core/program_clock.hpp"
#include "tests/pes_test_utils.hpp"
#include "tests/tot_test_utils.hpp"
#include "tsduck/tsTSPacket.h"

#include <catch2/catch_test_macros.hpp>

namespace {

using aribcap_dump::test::kCaptionPid;
using aribcap_dump::test::MakePesPacket;
using aribcap_dump::test::MakeTot;

constexpr std::uint32_t MakeLanguageCode(char first, char second, char third) {
    return (static_cast<std::uint32_t>(first) << 16) | (static_cast<std::uint32_t>(second) << 8) |
           static_cast<std::uint32_t>(third);
}

class FakeCaptionDecoder final : public aribcap_dump::CaptionDecoder {
   public:
    aribcaption::DecodeStatus status = aribcaption::DecodeStatus::kGotCaption;
    std::string text = "caption";
    std::uint32_t iso6392_language_code = MakeLanguageCode('j', 'p', 'n');
    std::int64_t wait_duration = aribcaption::DURATION_INDEFINITE;
    std::uint32_t second_language_code = 0;
    std::vector<aribcaption::CaptionRegion> regions;
    std::vector<std::uint8_t> last_payload;
    std::int64_t last_pts_ms = 0;
    std::size_t call_count = 0;

    [[nodiscard]] aribcaption::DecodeStatus Decode(const std::uint8_t* data, std::size_t size,
                                                   std::int64_t pts_ms,
                                                   aribcaption::DecodeResult& result) override {
        ++call_count;
        last_payload.assign(data, data + size);
        last_pts_ms = pts_ms;

        if (status == aribcaption::DecodeStatus::kGotCaption) {
            result.caption = std::make_unique<aribcaption::Caption>();
            result.caption->text = text;
            result.caption->iso6392_language_code = iso6392_language_code;
            result.caption->wait_duration = wait_duration;
            result.caption->regions = regions;
        }

        return status;
    }

    [[nodiscard]] std::uint32_t QueryISO6392LanguageCode(
        aribcaption::LanguageId language_id) const override {
        if (language_id == aribcaption::LanguageId::kSecond) {
            return second_language_code;
        }

        return iso6392_language_code;
    }
};

aribcap_dump::CaptionStreamInfo CaptionInfo() {
    return aribcap_dump::CaptionStreamInfo{
        .caption_type = aribcaption::CaptionType::kCaption,
        .profile = aribcaption::Profile::kProfileA,
    };
}

aribcaption::CaptionChar MakeChar(const char* u8str, aribcaption::ColorRGBA color) {
    aribcaption::CaptionChar ch;
    const auto size = std::strlen(u8str);
    REQUIRE(size < sizeof(ch.u8str));
    std::memcpy(ch.u8str, u8str, size);
    ch.u8str[size] = '\0';
    ch.text_color = color;

    return ch;
}

aribcaption::CaptionChar MakeDrcsChar(aribcaption::ColorRGBA color) {
    aribcaption::CaptionChar ch;
    ch.type = aribcaption::CaptionCharType::kDRCS;
    ch.text_color = color;

    return ch;
}

aribcaption::CaptionRegion MakeRegion(std::vector<aribcaption::CaptionChar> chars,
                                      bool is_ruby = false) {
    aribcaption::CaptionRegion region;
    region.chars = std::move(chars);
    region.is_ruby = is_ruby;

    return region;
}

class CaptionRecordEmitterFixture {
   public:
    aribcap_dump::ProgramClock clock;
    aribcap_dump::VectorOutputRecordSink sink;
    FakeCaptionDecoder* fake_decoder = nullptr;
    std::unique_ptr<aribcap_dump::CaptionRecordEmitter> emitter;

    void CreateEmitter(aribcap_dump::CaptionRecordEmitterOptions options = {}) {
        aribcap_dump::CaptionDecoderFactory decoder_factory =
            [&](aribcaption::LanguageId language_id)
            -> std::unique_ptr<aribcap_dump::CaptionDecoder> {
            auto decoder = std::make_unique<FakeCaptionDecoder>();

            if (language_id == aribcaption::LanguageId::kFirst) {
                fake_decoder = decoder.get();
            }

            return decoder;
        };
        emitter = std::make_unique<aribcap_dump::CaptionRecordEmitter>(
            sink, clock, CaptionInfo(), std::move(decoder_factory), options);
    }

    void RecordClockReference(std::int64_t pcr_90k, std::int64_t unix_ms) {
        ts::TSPacket packet;
        packet.init(0x100);
        REQUIRE(packet.setPCR(static_cast<std::uint64_t>(pcr_90k) * 300, true));

        clock.SetPcrPid(0x100);
        static_cast<void>(clock.RecordPcr(packet));

        clock.UpdateReferencePointFromTot(MakeTot(unix_ms));
    }

    void HandlePes(const std::vector<std::uint8_t>& payload,
                   std::optional<std::uint64_t> pts = std::nullopt) {
        REQUIRE(emitter != nullptr);

        emitter->HandlePes(MakePesPacket(payload, pts));
    }

    [[nodiscard]] const std::vector<aribcap_dump::OutputRecord>& Records() const {
        return sink.Records();
    }
};

}  // namespace

// -------------------------------------------------------------------------------------------------
// Caption emission tests
// -------------------------------------------------------------------------------------------------

TEST_CASE_METHOD(CaptionRecordEmitterFixture,
                 "CaptionRecordEmitter decodes PES payload and emits caption text as decoded") {
    constexpr std::int64_t kWallBase = 1'700'000'000'000;
    constexpr std::uint64_t kPts = 990'000;
    const std::vector<std::uint8_t> payload = {0x80, 0xFF, 0x42};

    RecordClockReference(900'000, kWallBase);
    CreateEmitter();
    fake_decoder->text = "\n caption text \t";

    HandlePes(payload, kPts);

    CHECK(fake_decoder->call_count == 1);
    CHECK(fake_decoder->last_payload == payload);
    CHECK(fake_decoder->last_pts_ms == 11'000);

    const auto& records = Records();
    REQUIRE(records.size() == 1);
    const auto* caption = std::get_if<aribcap_dump::CaptionRecord>(&records[0]);
    REQUIRE(caption != nullptr);
    CHECK(caption->pid == kCaptionPid);
    CHECK(caption->caption_type == aribcap_dump::CaptionRecordType::kCaption);
    REQUIRE(caption->language_code.has_value());
    CHECK(*caption->language_code == "jpn");
    REQUIRE(caption->time_ms.has_value());
    CHECK(*caption->time_ms == kWallBase + 1'000);
    CHECK_FALSE(caption->duration_ms.has_value());
    CHECK_FALSE(caption->color.has_value());
    CHECK(caption->ruby.empty());
    CHECK(caption->text == "\n caption text \t");
}

TEST_CASE_METHOD(CaptionRecordEmitterFixture,
                 "CaptionRecordEmitter passes through a definite caption duration") {
    CreateEmitter();
    fake_decoder->wait_duration = 5'000;

    HandlePes({0x80, 0xFF, 0x42}, 990'000);

    const auto& records = Records();
    REQUIRE(records.size() == 1);
    const auto* caption = std::get_if<aribcap_dump::CaptionRecord>(&records[0]);
    REQUIRE(caption != nullptr);
    REQUIRE(caption->duration_ms.has_value());
    CHECK(*caption->duration_ms == 5'000);
}

TEST_CASE_METHOD(CaptionRecordEmitterFixture,
                 "CaptionRecordEmitter emits null time and no-PTS sentinel when PES has no PTS") {
    CreateEmitter();

    HandlePes({0x01, 0x02});

    CHECK(fake_decoder->call_count == 1);
    CHECK(fake_decoder->last_pts_ms == std::numeric_limits<std::int64_t>::min());

    const auto& records = Records();
    REQUIRE(records.size() == 1);
    const auto* caption = std::get_if<aribcap_dump::CaptionRecord>(&records[0]);
    REQUIRE(caption != nullptr);
    CHECK_FALSE(caption->time_ms.has_value());
    CHECK_FALSE(caption->color.has_value());
    CHECK(caption->ruby.empty());
}

TEST_CASE_METHOD(CaptionRecordEmitterFixture,
                 "CaptionRecordEmitter stamps the decoded language code") {
    CreateEmitter();
    fake_decoder->iso6392_language_code = MakeLanguageCode('e', 'n', 'g');

    HandlePes({0x01, 0x02});

    const auto& records = Records();
    REQUIRE(records.size() == 1);
    const auto* caption = std::get_if<aribcap_dump::CaptionRecord>(&records[0]);
    REQUIRE(caption != nullptr);
    REQUIRE(caption->language_code.has_value());
    CHECK(*caption->language_code == "eng");
}

TEST_CASE_METHOD(CaptionRecordEmitterFixture,
                 "CaptionRecordEmitter sets superimpose caption type") {
    aribcap_dump::CaptionDecoderFactory decoder_factory =
        [&](aribcaption::LanguageId language_id) -> std::unique_ptr<aribcap_dump::CaptionDecoder> {
        auto decoder = std::make_unique<FakeCaptionDecoder>();

        if (language_id == aribcaption::LanguageId::kFirst) {
            fake_decoder = decoder.get();
        }

        return decoder;
    };
    emitter = std::make_unique<aribcap_dump::CaptionRecordEmitter>(
        sink, clock,
        aribcap_dump::CaptionStreamInfo{
            .caption_type = aribcaption::CaptionType::kSuperimpose,
            .profile = aribcaption::Profile::kProfileA,
        },
        std::move(decoder_factory));

    HandlePes({0x01, 0x02});

    const auto& records = Records();
    REQUIRE(records.size() == 1);
    const auto* caption = std::get_if<aribcap_dump::CaptionRecord>(&records[0]);
    REQUIRE(caption != nullptr);
    CHECK(caption->caption_type == aribcap_dump::CaptionRecordType::kSuperimpose);
}

TEST_CASE_METHOD(CaptionRecordEmitterFixture,
                 "CaptionRecordEmitter emits compact caption color and ruby metadata") {
    CreateEmitter();
    fake_decoder->text = "明日は晴れです";
    fake_decoder->regions = {
        MakeRegion({
            MakeChar("明", aribcaption::ColorRGBA(255, 255, 0, 255)),
            MakeChar("日", aribcaption::ColorRGBA(255, 255, 255, 255)),
        }),
        MakeRegion(
            {
                MakeChar("あ", aribcaption::ColorRGBA(0, 255, 255, 255)),
                MakeChar("した", aribcaption::ColorRGBA(0, 255, 255, 255)),
            },
            true),
    };

    HandlePes({0x01, 0x02});

    const auto& records = Records();
    REQUIRE(records.size() == 1);
    const auto* caption = std::get_if<aribcap_dump::CaptionRecord>(&records[0]);
    REQUIRE(caption != nullptr);
    CHECK(caption->text == "明日は晴れです");
    REQUIRE(caption->color.has_value());
    CHECK(*caption->color == "0xffff00ff");
    CHECK(caption->ruby == std::vector<std::string>{"あした"});
}

TEST_CASE_METHOD(CaptionRecordEmitterFixture,
                 "CaptionRecordEmitter samples color from unmapped DRCS captions") {
    CreateEmitter();
    fake_decoder->text = "〓";
    fake_decoder->regions = {
        MakeRegion({
            MakeDrcsChar(aribcaption::ColorRGBA(0, 255, 0, 255)),
        }),
    };

    HandlePes({0x01, 0x02});

    const auto& records = Records();
    REQUIRE(records.size() == 1);
    const auto* caption = std::get_if<aribcap_dump::CaptionRecord>(&records[0]);
    REQUIRE(caption != nullptr);
    CHECK(caption->text == "〓");
    REQUIRE(caption->color.has_value());
    CHECK(*caption->color == "0x00ff00ff");
    CHECK(caption->ruby.empty());
}

TEST_CASE_METHOD(CaptionRecordEmitterFixture,
                 "CaptionRecordEmitter does not sample color from ruby-only regions") {
    CreateEmitter();
    fake_decoder->text = "明日";
    fake_decoder->regions = {
        MakeRegion(
            {
                MakeChar("あ", aribcaption::ColorRGBA(0, 255, 255, 255)),
                MakeChar("した", aribcaption::ColorRGBA(0, 255, 255, 255)),
            },
            true),
    };

    HandlePes({0x01, 0x02});

    const auto& records = Records();
    REQUIRE(records.size() == 1);
    const auto* caption = std::get_if<aribcap_dump::CaptionRecord>(&records[0]);
    REQUIRE(caption != nullptr);
    CHECK_FALSE(caption->color.has_value());
    CHECK(caption->ruby == std::vector<std::string>{"あした"});
}

// -------------------------------------------------------------------------------------------------
// Caption suppression tests
// -------------------------------------------------------------------------------------------------

TEST_CASE_METHOD(CaptionRecordEmitterFixture,
                 "CaptionRecordEmitter ignores decoder statuses without captions") {
    CreateEmitter();
    fake_decoder->status = aribcaption::DecodeStatus::kNoCaption;

    HandlePes({0x01, 0x02});

    CHECK(fake_decoder->call_count == 1);
    CHECK(Records().empty());
}

TEST_CASE_METHOD(CaptionRecordEmitterFixture,
                 "CaptionRecordEmitter ignores captions with empty text") {
    CreateEmitter();
    fake_decoder->text = "";

    HandlePes({0x01, 0x02});

    CHECK(fake_decoder->call_count == 1);
    CHECK(Records().empty());
}

TEST_CASE_METHOD(CaptionRecordEmitterFixture,
                 "CaptionRecordEmitter can emit captions with empty text") {
    CreateEmitter(aribcap_dump::CaptionRecordEmitterOptions{.emit_empty_captions = true});
    fake_decoder->text = "";
    fake_decoder->wait_duration = 250;

    HandlePes({0x01, 0x02});

    const auto& records = Records();
    REQUIRE(records.size() == 1);
    const auto* caption = std::get_if<aribcap_dump::CaptionRecord>(&records[0]);
    REQUIRE(caption != nullptr);
    CHECK(caption->text.empty());
    REQUIRE(caption->duration_ms.has_value());
    CHECK(*caption->duration_ms == 250);
}

TEST_CASE_METHOD(CaptionRecordEmitterFixture,
                 "CaptionRecordEmitter creates the second decoder after language detection") {
    FakeCaptionDecoder* first_decoder = nullptr;
    FakeCaptionDecoder* second_decoder = nullptr;
    aribcap_dump::CaptionDecoderFactory decoder_factory =
        [&](aribcaption::LanguageId language_id) -> std::unique_ptr<aribcap_dump::CaptionDecoder> {
        auto decoder = std::make_unique<FakeCaptionDecoder>();
        decoder->status = aribcaption::DecodeStatus::kNoCaption;

        if (language_id == aribcaption::LanguageId::kFirst) {
            first_decoder = decoder.get();
            first_decoder->second_language_code = MakeLanguageCode('e', 'n', 'g');
        } else {
            second_decoder = decoder.get();
        }

        return decoder;
    };
    emitter = std::make_unique<aribcap_dump::CaptionRecordEmitter>(sink, clock, CaptionInfo(),
                                                                   std::move(decoder_factory));

    HandlePes({0x01, 0x02});

    REQUIRE(first_decoder != nullptr);
    REQUIRE(second_decoder != nullptr);
    CHECK(first_decoder->call_count == 1);
    CHECK(second_decoder->call_count == 1);
    CHECK(Records().empty());
}

// -------------------------------------------------------------------------------------------------
// Decoder error diagnostic tests
// -------------------------------------------------------------------------------------------------

TEST_CASE_METHOD(CaptionRecordEmitterFixture,
                 "CaptionRecordEmitter emits diagnostic when decoder reports error") {
    CreateEmitter();
    fake_decoder->status = aribcaption::DecodeStatus::kError;

    HandlePes({0x01, 0x02});

    const auto& records = Records();
    REQUIRE(records.size() == 1);
    const auto* diagnostic = std::get_if<aribcap_dump::DiagnosticRecord>(&records[0]);
    REQUIRE(diagnostic != nullptr);
    const auto* error = std::get_if<aribcap_dump::CaptionDecodeError>(&diagnostic->kind);
    REQUIRE(error != nullptr);
    CHECK(error->pid == kCaptionPid);
}

TEST_CASE_METHOD(
    CaptionRecordEmitterFixture,
    "CaptionRecordEmitter emits one diagnostic when decoders would report the same error") {
    FakeCaptionDecoder* first_decoder = nullptr;
    FakeCaptionDecoder* second_decoder = nullptr;
    aribcap_dump::CaptionDecoderFactory decoder_factory =
        [&](aribcaption::LanguageId language_id) -> std::unique_ptr<aribcap_dump::CaptionDecoder> {
        auto decoder = std::make_unique<FakeCaptionDecoder>();
        decoder->status = aribcaption::DecodeStatus::kError;

        if (language_id == aribcaption::LanguageId::kFirst) {
            first_decoder = decoder.get();
            first_decoder->second_language_code = MakeLanguageCode('e', 'n', 'g');
        } else {
            second_decoder = decoder.get();
        }

        return decoder;
    };
    emitter = std::make_unique<aribcap_dump::CaptionRecordEmitter>(sink, clock, CaptionInfo(),
                                                                   std::move(decoder_factory));

    HandlePes({0x01, 0x02});

    REQUIRE(first_decoder != nullptr);
    CHECK(second_decoder == nullptr);
    CHECK(first_decoder->call_count == 1);

    const auto& records = Records();
    REQUIRE(records.size() == 1);
    const auto* diagnostic = std::get_if<aribcap_dump::DiagnosticRecord>(&records[0]);
    REQUIRE(diagnostic != nullptr);
    const auto* error = std::get_if<aribcap_dump::CaptionDecodeError>(&diagnostic->kind);
    REQUIRE(error != nullptr);
    CHECK(error->pid == kCaptionPid);
}

TEST_CASE_METHOD(CaptionRecordEmitterFixture,
                 "CaptionRecordEmitter emits one diagnostic when a later decoder reports error") {
    FakeCaptionDecoder* first_decoder = nullptr;
    FakeCaptionDecoder* second_decoder = nullptr;
    aribcap_dump::CaptionDecoderFactory decoder_factory =
        [&](aribcaption::LanguageId language_id) -> std::unique_ptr<aribcap_dump::CaptionDecoder> {
        auto decoder = std::make_unique<FakeCaptionDecoder>();

        if (language_id == aribcaption::LanguageId::kFirst) {
            first_decoder = decoder.get();
            first_decoder->status = aribcaption::DecodeStatus::kNoCaption;
            first_decoder->second_language_code = MakeLanguageCode('e', 'n', 'g');
        } else {
            second_decoder = decoder.get();
            second_decoder->status = aribcaption::DecodeStatus::kError;
        }

        return decoder;
    };
    emitter = std::make_unique<aribcap_dump::CaptionRecordEmitter>(sink, clock, CaptionInfo(),
                                                                   std::move(decoder_factory));

    HandlePes({0x01, 0x02});

    REQUIRE(first_decoder != nullptr);
    REQUIRE(second_decoder != nullptr);
    CHECK(first_decoder->call_count == 1);
    CHECK(second_decoder->call_count == 1);

    const auto& records = Records();
    REQUIRE(records.size() == 1);
    const auto* diagnostic = std::get_if<aribcap_dump::DiagnosticRecord>(&records[0]);
    REQUIRE(diagnostic != nullptr);
    const auto* error = std::get_if<aribcap_dump::CaptionDecodeError>(&diagnostic->kind);
    REQUIRE(error != nullptr);
    CHECK(error->pid == kCaptionPid);
}
