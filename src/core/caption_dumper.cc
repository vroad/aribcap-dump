// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/caption_dumper.hpp"

#include <map>

#include "core/caption_classifier.hpp"
#include "core/eit_parser.hpp"
#include "core/output_record.hpp"
#include "core/output_record_sink.hpp"
#include "tsduck/tsARIBCharset.h"
#include "tsduck/tsBinaryTable.h"
#include "tsduck/tsEIT.h"
#include "tsduck/tsPAT.h"
#include "tsduck/tsPMT.h"
#include "tsduck/tsTID.h"
#include "tsduck/tsTOT.h"
#include "tsduck/tsTS.h"

namespace aribcap_dump {
namespace {

[[nodiscard]] ts::PIDSet InitialSectionPids() {
    ts::PIDSet pids;
    pids.set(ts::PID_PAT);
    pids.set(ts::PID_EIT);
    pids.set(ts::PID_ISDB_EIT_3);
    pids.set(ts::PID_TOT);

    return pids;
}

}  // namespace

CaptionDumper::CaptionDumper(std::uint16_t target_sid, OutputRecordSink& sink,
                             CaptionDumperOptions options)
    : target_sid_(target_sid),
      options_(options),
      sink_(sink),
      context_(),
      reserved_section_pids_(InitialSectionPids()),
      section_demux_(context_, this, nullptr, reserved_section_pids_),
      pes_demux_(context_, this, ts::NoPID()),
      program_clock_(),
      eit_emitter_(sink, options.eit_output) {
    context_.setDefaultCharsetIn(&ts::ARIBCharset::B24);
}

void CaptionDumper::FeedPacket(const ts::TSPacket& packet) {
    if (!packet.hasValidSync()) {
        return;
    }

    if (const auto discontinuity = program_clock_.RecordPcr(packet)) {
        sink_.Emit(DiagnosticRecord{*discontinuity});
    }

    section_demux_.feedPacket(packet);
    pes_demux_.feedPacket(packet);
}

void CaptionDumper::Flush() {
    pes_demux_.flushUnboundedPES();
}

void CaptionDumper::handleTable(ts::SectionDemux& /*demux*/, const ts::BinaryTable& table) {
    switch (table.tableId()) {
        case ts::TID_PAT:
            HandlePat(table);
            break;
        case ts::TID_PMT:
            HandlePmt(table);
            break;
        case ts::TID_TOT:
            HandleTot(table);
            break;
        case ts::TID_TDT:
            // TDT arrives here too because it shares PID_TOT's PID, but don't process it: real ARIB
            // streams can carry broken TDT values that reset the wall clock, and TOT alone is
            // enough for clock sync.
            //
            // See https://github.com/mirakc/mirakc-arib/issues/118 for details.
            break;
        case ts::TID_EIT_PF_ACT:
            HandleEit(table);
            break;
        default:
            break;
    }
}

void CaptionDumper::handlePESPacket(ts::PESDemux& /*demux*/, const ts::PESPacket& packet) {
    const auto emitters = caption_emitters_.find(packet.sourcePID());

    if (emitters == caption_emitters_.end()) {
        return;
    }

    emitters->second.HandlePes(packet);
}

void CaptionDumper::HandlePat(const ts::BinaryTable& table) {
    const ts::PAT pat(context_, table);

    if (!pat.isValid()) {
        return;
    }

    const auto entry = pat.pmts.find(target_sid_);

    if (entry == pat.pmts.end()) {
        // The target service is no longer listed (taken off air or
        // renumbered away); tear down state tied to its old PMT PID. This is
        // a no-op if the service was already torn down.
        TeardownTargetService();

        return;
    }

    const ts::PID pmt_pid = entry->second;

    // A malformed TS may advertise a PMT PID that is already demuxed via
    // `reserved_section_pids_`; reject it and report the collision.
    //
    // Without this check, `addPID` would track a PAT/EIT/TDT PID as
    // `current_pmt_pid_`. The next PAT update would then call `removePID` on
    // that PID, permanently breaking demuxing for that table.
    if (reserved_section_pids_.test(pmt_pid)) {
        sink_.Emit(DiagnosticRecord{ReservedPmtPidCollision{static_cast<std::uint16_t>(pmt_pid)}});

        return;
    }

    if (current_pmt_pid_ == pmt_pid) {
        return;
    }

    if (current_pmt_pid_.has_value()) {
        TeardownTargetService();
    }

    current_pmt_pid_ = pmt_pid;
    section_demux_.addPID(pmt_pid);
}

void CaptionDumper::HandlePmt(const ts::BinaryTable& table) {
    const ts::PMT pmt(context_, table);

    if (!pmt.isValid() || pmt.service_id != target_sid_) {
        return;
    }

    program_clock_.SetPcrPid(pmt.pcr_pid);
    SyncCaptionStreams(table.sourcePID(), pmt.streams);
}

void CaptionDumper::HandleTot(const ts::BinaryTable& table) {
    const ts::TOT tot(context_, table);

    program_clock_.UpdateReferencePointFromTot(tot);
}

void CaptionDumper::HandleEit(const ts::BinaryTable& table) {
    // `table_id_extension` is `service_id` for EIT, and every section merged into one
    // `BinaryTable` shares it. Filtering here is equivalent to checking `ts::EIT::service_id`
    // after deserializing, but avoids having to deserialize and descriptor-decode every other
    // service's EIT p/f table on this PID.
    if (!table.isValid() || table.tableIdExtension() != target_sid_) {
        return;
    }

    const DeserializedEit deserialized = DeserializeEit(context_, table);

    if (!deserialized.eit.isValid()) {
        return;
    }

    eit_emitter_.HandleEit(context_, deserialized);
}

void CaptionDumper::SyncCaptionStreams(ts::PID pmt_pid, const ts::PMT::StreamMap& streams) {
    std::map<ts::PID, CaptionStreamInfo> desired;

    for (const auto& [es_pid, stream] : streams) {
        auto caption = ClassifyCaptionStream(context_, pmt_pid, stream);

        if (caption.has_value()) {
            desired.emplace(es_pid, *caption);
        }
    }

    for (auto current = caption_emitters_.begin(); current != caption_emitters_.end();) {
        if (desired.find(current->first) == desired.end()) {
            const auto pid = current->first;
            ++current;
            UnregisterCaptionStream(pid);
            continue;
        }

        ++current;
    }

    for (const auto& [pid, info] : desired) {
        auto current = caption_emitters_.find(pid);

        if (current == caption_emitters_.end()) {
            RegisterCaptionStream(pid, info);
            continue;
        }

        if (current->second.Info() != info) {
            UnregisterCaptionStream(pid);
            RegisterCaptionStream(pid, info);
        }
    }
}

void CaptionDumper::RegisterCaptionStream(ts::PID pid, CaptionStreamInfo info) {
    caption_emitters_.try_emplace(
        pid, sink_, program_clock_, info,
        CaptionRecordEmitterOptions{.emit_empty_captions = options_.emit_empty_captions});

    pes_demux_.addPID(pid);
}

void CaptionDumper::UnregisterCaptionStream(ts::PID pid) {
    pes_demux_.removePID(pid);
    caption_emitters_.erase(pid);
}

void CaptionDumper::TeardownTargetService() {
    if (current_pmt_pid_.has_value()) {
        section_demux_.removePID(*current_pmt_pid_);
        current_pmt_pid_.reset();
    }

    while (!caption_emitters_.empty()) {
        const auto pid = caption_emitters_.begin()->first;
        UnregisterCaptionStream(pid);
    }
}

}  // namespace aribcap_dump
