// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <map>
#include <optional>

#include "core/caption_record_emitter.hpp"
#include "core/eit_record_emitter.hpp"
#include "core/program_clock.hpp"
#include "tsduck/tsBinaryTable.h"
#include "tsduck/tsDuckContext.h"
#include "tsduck/tsPESDemux.h"
#include "tsduck/tsPESHandlerInterface.h"
#include "tsduck/tsPMT.h"
#include "tsduck/tsSectionDemux.h"
#include "tsduck/tsTS.h"
#include "tsduck/tsTSPacket.h"
#include "tsduck/tsTableHandlerInterface.h"

namespace aribcap_dump {

class OutputRecordSink;
class CaptionDumperTestAccessor;

struct CaptionDumperOptions {
    bool emit_empty_captions = false;
    EitOutputMode eit_output = EitOutputMode::kPresent;
};

// Per-service coordinator that drives TSDuck demuxers over an MPEG-TS packet
// stream.
//
// Demuxed tables/PES packets feed internal bookkeeping and member
// components; both this class and its member components may emit `OutputRecord`s
// through `OutputRecordSink`.
class CaptionDumper final : private ts::TableHandlerInterface, private ts::PESHandlerInterface {
   public:
    CaptionDumper(std::uint16_t target_sid, OutputRecordSink& sink,
                  CaptionDumperOptions options = {});

    void FeedPacket(const ts::TSPacket& packet);
    // Emits any unbounded PES packet still buffered in the PES demuxer.
    //
    // A bounded PES packet (with an explicit PES_packet_length) is delivered as soon as its
    // declared length arrives. An unbounded PES packet (PES_packet_length == 0) has no declared
    // end. TSDuck delivers it only when the next PES packet on the same PID starts.
    //
    // The stream's final unbounded PES therefore is not delivered automatically at EOF.
    // Calling this method flushes it.
    void Flush();

   private:
    friend class CaptionDumperTestAccessor;

    void handleTable(ts::SectionDemux& demux, const ts::BinaryTable& table) override;
    void handlePESPacket(ts::PESDemux& demux, const ts::PESPacket& packet) override;

    void HandlePat(const ts::BinaryTable& table);
    void HandlePmt(const ts::BinaryTable& table);
    void HandleTot(const ts::BinaryTable& table);
    void HandleEit(const ts::BinaryTable& table);
    // Reconciles the active caption emitters against the PMT's current stream
    // list: registers streams that are new or changed and unregisters ones
    // that disappeared or changed.
    void SyncCaptionStreams(ts::PID pmt_pid, const ts::PMT::StreamMap& streams);
    // Creates a caption emitter for `pid` and starts demuxing its PES packets.
    void RegisterCaptionStream(ts::PID pid, aribcaption::Profile profile);
    // Stops demuxing `pid`'s PES packets and destroys its caption emitter.
    void UnregisterCaptionStream(ts::PID pid);
    // Tears down all state tied to the target service's current PMT PID,
    // including its registered caption emitters.
    void TeardownTargetService();

    std::uint16_t target_sid_;
    CaptionDumperOptions options_;
    OutputRecordSink& sink_;
    ts::DuckContext context_;
    const ts::PIDSet reserved_section_pids_;
    ts::SectionDemux section_demux_;
    std::optional<ts::PID> current_pmt_pid_;
    ts::PESDemux pes_demux_;

    // Shared timebase the caption emitters below query to stamp captions with
    // wall-clock time.
    ProgramClock program_clock_;
    // Emitters of `OutputRecord`s built from content demuxed by the demuxers above.
    EitRecordEmitter eit_emitter_;
    std::map<ts::PID, CaptionRecordEmitter> caption_emitters_;
};

}  // namespace aribcap_dump
