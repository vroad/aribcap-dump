// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/eit_record_emitter.hpp"

#include <tuple>

#include "core/eit_parser.hpp"
#include "core/output_record_sink.hpp"

namespace aribcap_dump {

EitRecordEmitter::EitRecordEmitter(OutputRecordSink& sink) : sink_(sink) {}

bool EitRecordEmitter::EitEventKey::operator<(const EitEventKey& rhs) const {
    return std::tie(section, event_id) < std::tie(rhs.section, rhs.event_id);
}

void EitRecordEmitter::HandleEit(ts::DuckContext& context, const DeserializedEit& deserialized) {
    for (const auto& record : ParseEit(context, deserialized)) {
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
