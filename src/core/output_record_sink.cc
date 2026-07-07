// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/output_record_sink.hpp"

#include <ostream>
#include <vector>

namespace aribcap_dump {

JsonlOutputRecordSink::JsonlOutputRecordSink(std::ostream& stream) : stream_(stream) {}

void JsonlOutputRecordSink::Emit(const OutputRecord& record) {
    stream_ << ToJsonLine(record) << '\n';
    stream_.flush();
}

void VectorOutputRecordSink::Emit(const OutputRecord& record) {
    records_.push_back(record);
}

const std::vector<OutputRecord>& VectorOutputRecordSink::Records() const {
    return records_;
}

}  // namespace aribcap_dump
