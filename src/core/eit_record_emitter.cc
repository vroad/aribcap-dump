// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/eit_record_emitter.hpp"

#include "core/eit_parser.hpp"
#include "core/output_record_sink.hpp"

namespace aribcap_dump {

EitRecordEmitter::EitRecordEmitter(OutputRecordSink& sink, EitOutputMode output_mode)
    : sink_(sink), output_mode_(output_mode) {}

void EitRecordEmitter::HandleEit(ts::DuckContext& context, const DeserializedEit& deserialized) {
    if (last_emitted_version_ == deserialized.version) {
        return;
    }

    last_emitted_version_ = deserialized.version;

    for (const auto& record : ParseEit(context, deserialized)) {
        const bool emit =
            output_mode_ == EitOutputMode::kPresentFollowing ||
            (output_mode_ == EitOutputMode::kPresent && record.section == EitSection::kPresent) ||
            (output_mode_ == EitOutputMode::kFollowing && record.section == EitSection::kFollowing);

        if (!emit) {
            continue;
        }

        sink_.Emit(record);
    }
}

}  // namespace aribcap_dump
