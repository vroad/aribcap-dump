// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/eit_record_emitter.hpp"

#include <tuple>

#include "core/eit_parser.hpp"
#include "core/output_record_sink.hpp"

namespace aribcap_dump {

EitRecordEmitter::EitRecordEmitter(OutputRecordSink& sink, EitOutputMode output_mode)
    : sink_(sink), output_mode_(output_mode) {}

bool EitRecordEmitter::EitEventKey::operator<(const EitEventKey& rhs) const {
    return std::tie(section, event_id) < std::tie(rhs.section, rhs.event_id);
}

void EitRecordEmitter::HandleEit(ts::DuckContext& context, const DeserializedEit& deserialized) {
    for (const auto& record : ParseEit(context, deserialized)) {
        const bool emit =
            output_mode_ == EitOutputMode::kPresentFollowing ||
            (output_mode_ == EitOutputMode::kPresent && record.section == EitSection::kPresent) ||
            (output_mode_ == EitOutputMode::kFollowing && record.section == EitSection::kFollowing);

        if (!emit) {
            continue;
        }

        const EitEventKey key{
            .section = record.section,
            .event_id = record.event_id,
        };

        auto [it, inserted] = last_emitted_version_.try_emplace(key, record.version);

        if (!inserted) {
            if (it->second == record.version) {
                continue;
            }

            it->second = record.version;
        }

        sink_.Emit(record);
    }
}

}  // namespace aribcap_dump
