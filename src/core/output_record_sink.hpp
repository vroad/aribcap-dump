// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <iosfwd>
#include <vector>

#include "core/output_record.hpp"

namespace aribcap_dump {

// Output interface for `OutputRecord`s; all emitted records go through one of these.
class OutputRecordSink {
   public:
    virtual ~OutputRecordSink() = default;
    virtual void Emit(const OutputRecord& record) = 0;
};

// JSONL output sink that writes each `OutputRecord` as one line to `stream`. Write
// failures surface through `stream`'s own error state; the caller owns error
// detection and reporting.
class JsonlOutputRecordSink final : public OutputRecordSink {
   public:
    explicit JsonlOutputRecordSink(std::ostream& stream);

    void Emit(const OutputRecord& record) override;

   private:
    std::ostream& stream_;
};

// In-memory output record sink for inspecting emitted `OutputRecord`s in tests.
class VectorOutputRecordSink final : public OutputRecordSink {
   public:
    void Emit(const OutputRecord& record) override;
    [[nodiscard]] const std::vector<OutputRecord>& Records() const;

   private:
    std::vector<OutputRecord> records_;
};

}  // namespace aribcap_dump
