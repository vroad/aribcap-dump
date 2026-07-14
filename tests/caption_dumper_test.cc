// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/caption_dumper.hpp"

#include <map>
#include <vector>

#include "core/caption_classifier.hpp"
#include "core/caption_record_emitter.hpp"
#include "core/output_record_sink.hpp"
#include "core/program_clock.hpp"
#include "tests/eit_test_utils.hpp"
#include "tests/pmt_test_utils.hpp"
#include "tests/tot_test_utils.hpp"
#include "tscore/tsTime.h"
#include "tsduck/tsAbstractTable.h"
#include "tsduck/tsBinaryTable.h"
#include "tsduck/tsDuckContext.h"
#include "tsduck/tsEIT.h"
#include "tsduck/tsOneShotPacketizer.h"
#include "tsduck/tsPAT.h"
#include "tsduck/tsPESDemux.h"
#include "tsduck/tsPMT.h"
#include "tsduck/tsSection.h"
#include "tsduck/tsSectionDemux.h"
#include "tsduck/tsStreamType.h"
#include "tsduck/tsTOT.h"
#include "tsduck/tsTS.h"
#include "tsduck/tsTSPacket.h"

#include <catch2/catch_test_macros.hpp>

namespace aribcap_dump {

// Test-only accessor: exposes CaptionDumper's private demuxers and caption emitters
// so tests can assert on which PIDs are currently demuxed and tracked.
class CaptionDumperTestAccessor {
   public:
    static ts::SectionDemux& SectionDemux(CaptionDumper& caption_dumper) {
        return caption_dumper.section_demux_;
    }

    static ts::PESDemux& PesDemux(CaptionDumper& caption_dumper) {
        return caption_dumper.pes_demux_;
    }

    static const std::map<ts::PID, CaptionRecordEmitter>& CaptionRecordEmitters(
        CaptionDumper& caption_dumper) {
        return caption_dumper.caption_emitters_;
    }

    static aribcap_dump::ProgramClock& ProgramClock(CaptionDumper& caption_dumper) {
        return caption_dumper.program_clock_;
    }
};

}  // namespace aribcap_dump

namespace {

// One PID layout shared by all tests: the caption_dumper targets service `kSid`, whose
// PMT arrives on `kPmtPid` (`kSecondPmtPid` after a PAT change) and lists
// caption components on `kCaptionPid`/`kSecondCaptionPid`.
constexpr std::uint16_t kSid = 1024;
constexpr ts::PID kPmtPid = 0x200;
constexpr ts::PID kSecondPmtPid = 0x201;
constexpr ts::PID kPcrPid = 0x1FFE;
constexpr ts::PID kCaptionPid = 0x100;
constexpr ts::PID kSecondCaptionPid = 0x101;
constexpr ts::PID kVideoPid = 0x102;

// Reads the packets `packetizer` has produced and assigns them sequential continuity
// counters, starting from `cc`.
//
// `cc` is a mutable reference the caller reuses across calls, so that continuity
// counters keep increasing instead of restarting at each call. TSDuck's
// `SectionDemux` treats a repeated continuity-counter value on a PID as a
// duplicate packet and silently drops it. Packets from independently
// constructed packetizers on the same PID must therefore use distinct CCs.
ts::TSPacketVector GetPacketsWithSequentialCC(ts::OneShotPacketizer& packetizer, std::uint8_t& cc) {
    ts::TSPacketVector packets;
    packetizer.getPackets(packets);

    for (auto& packet : packets) {
        packet.setCC(cc);
        cc = (cc + 1) & 0x0F;
    }

    return packets;
}

// Builds the TS packets for a single-section `table` on `pid`.
ts::TSPacketVector PacketizeTable(ts::DuckContext& context, const ts::AbstractTable& table,
                                  ts::PID pid, std::uint8_t& cc) {
    ts::OneShotPacketizer packetizer(context, pid);
    packetizer.addTable(context, table);
    return GetPacketsWithSequentialCC(packetizer, cc);
}

// Same as `PacketizeTable`, but for a `ts::BinaryTable` built directly from raw sections
// (see `MakePfEitWithEmptyPresentSection`) instead of an `ts::AbstractTable` subclass.
ts::TSPacketVector PacketizeBinaryTable(ts::DuckContext& context, const ts::BinaryTable& table,
                                        ts::PID pid, std::uint8_t& cc) {
    ts::OneShotPacketizer packetizer(context, pid);
    packetizer.addTable(table);
    return GetPacketsWithSequentialCC(packetizer, cc);
}

void AddCaptionStream(ts::DuckContext& context, ts::PMT& pmt, ts::PID pid) {
    aribcap_dump::test::AddPrivatePesStream(context, pmt, pid, 0x30, 0x0008);
}

void AddSuperimposeStream(ts::DuckContext& context, ts::PMT& pmt, ts::PID pid) {
    aribcap_dump::test::AddPrivatePesStream(context, pmt, pid, 0x38, 0x0008);
}

// Builds an actual-TS present/following `ts::EIT` with two events, one per
// section, so that the packetized table is complete and the section demux
// delivers it.
ts::EIT MakePfEit(std::uint16_t service_id, std::uint8_t version) {
    ts::EIT eit = aribcap_dump::test::MakePfEit(service_id, version);

    for (std::uint16_t event_id : {0x1234, 0x1235}) {
        auto& event = eit.events.newEntry();
        event.event_id = event_id;
        event.start_time = ts::Time(2020, 1, 1, 0, 0, 0);
        event.duration = cn::seconds(90 * 60);
    }

    return eit;
}

// Builds a genuine 2-section present/following EIT with an empty present section.
//
// When nothing is on air during a programme gap (the interval between two scheduled programmes),
// the present section (section 0) carries no event; the following section (section 1) still
// announces the next programme in `event_id`.
//
// Because `ts::EIT::serialize()` always packs a single event into section 0, reproducing an
// empty present section with the event in the following section requires building the two
// sections directly.
//
// The workaround: serialize the event once to get real, TSDuck-encoded payload bytes (no
// hand-rolled event-loop encoding), then re-wrap those bytes into a present/following section pair
// via the section-level constructor.
ts::BinaryTable MakePfEitWithEmptyPresentSection(ts::DuckContext& context, std::uint16_t service_id,
                                                 std::uint16_t event_id, std::uint8_t version) {
    ts::EIT following_only = aribcap_dump::test::MakePfEit(service_id, version);
    auto& event = following_only.events.newEntry();
    event.event_id = event_id;
    event.start_time = ts::Time(2020, 1, 1, 1, 30, 0);
    event.duration = cn::seconds(90 * 60);

    ts::BinaryTable single_section_table;
    following_only.serialize(context, single_section_table);
    const auto& following_section = single_section_table.sectionAt(0);

    ts::BinaryTable table;
    table.addNewSection(following_section->tableId(), /*is_private_section=*/false, service_id,
                        version,
                        /*is_current=*/true, /*section_number=*/0, /*last_section_number=*/1,
                        following_section->payload(), ts::EIT::EIT_PAYLOAD_FIXED_SIZE);
    table.addNewSection(following_section->tableId(), /*is_private_section=*/false, service_id,
                        version,
                        /*is_current=*/true, /*section_number=*/1, /*last_section_number=*/1,
                        following_section->payload(), following_section->payloadSize());

    return table;
}

// Test fixture that owns the caption_dumper under test plus the state every test
// feeds it through: a DuckContext for packetizing tables, the capturing sink,
// and per-PID continuity counters.
struct CaptionDumperFixture {
    ts::DuckContext context;
    aribcap_dump::VectorOutputRecordSink sink;
    aribcap_dump::CaptionDumper caption_dumper;
    std::uint8_t pat_cc = 0;
    std::uint8_t pmt_cc = 0;
    std::uint8_t eit_cc = 0;
    std::uint8_t tot_cc = 0;

    CaptionDumperFixture() : caption_dumper(kSid, sink) {}

    ts::SectionDemux& SectionDemux() {
        return aribcap_dump::CaptionDumperTestAccessor::SectionDemux(caption_dumper);
    }

    ts::PESDemux& PesDemux() {
        return aribcap_dump::CaptionDumperTestAccessor::PesDemux(caption_dumper);
    }

    const std::map<ts::PID, aribcap_dump::CaptionRecordEmitter>& CaptionRecordEmitters() {
        return aribcap_dump::CaptionDumperTestAccessor::CaptionRecordEmitters(caption_dumper);
    }

    aribcap_dump::ProgramClock& ProgramClock() {
        return aribcap_dump::CaptionDumperTestAccessor::ProgramClock(caption_dumper);
    }

    // Feeds a PAT advertising `pmt_pid` for `sid`.
    void FeedPat(std::uint8_t version, std::uint16_t sid, ts::PID pmt_pid) {
        ts::PAT pat(version);
        pat.pmts[sid] = pmt_pid;
        Feed(PacketizeTable(context, pat, ts::PID_PAT, pat_cc));
    }

    void FeedPmt(const ts::PMT& pmt, ts::PID pmt_pid) {
        Feed(PacketizeTable(context, pmt, pmt_pid, pmt_cc));
    }

    void FeedEit(const ts::EIT& eit) {
        Feed(PacketizeTable(context, eit, ts::PID_EIT, eit_cc));
    }

    void FeedEitTable(const ts::BinaryTable& table) {
        Feed(PacketizeBinaryTable(context, table, ts::PID_EIT, eit_cc));
    }

    // Feeds an ISDB TOT for `unix_ms` on `ts::PID_TOT`.
    void FeedTot(std::int64_t unix_ms) {
        Feed(PacketizeTable(context, aribcap_dump::test::MakeTot(unix_ms), ts::PID_TOT, tot_cc));
    }

    // Feeds a single TS packet on `pid` carrying `pcr_90k` (converted to 27 MHz PCR units),
    // bypassing the section/PES demuxers so it reaches `CaptionDumper::FeedPacket`'s PCR
    // bookkeeping directly.
    void FeedPcr(ts::PID pid, std::int64_t pcr_90k) {
        ts::TSPacket packet;
        packet.init(pid);
        REQUIRE(packet.setPCR(static_cast<std::uint64_t>(pcr_90k) * 300, true));

        caption_dumper.FeedPacket(packet);
    }

    void Feed(const ts::TSPacketVector& packets) {
        for (const auto& packet : packets) {
            caption_dumper.FeedPacket(packet);
        }
    }
};

}  // namespace

// -------------------------------------------------------------------------------------------------
// PAT demux tracking tests
// -------------------------------------------------------------------------------------------------

TEST_CASE_METHOD(CaptionDumperFixture,
                 "CaptionDumper stops demuxing the old PMT PID once the PAT advertises a new one") {
    auto& demux = SectionDemux();

    FeedPat(0, kSid, kPmtPid);

    REQUIRE(demux.hasPID(kPmtPid));

    FeedPat(1, kSid, kSecondPmtPid);

    CHECK(demux.hasPID(kSecondPmtPid));
    CHECK_FALSE(demux.hasPID(kPmtPid));
}

TEST_CASE_METHOD(CaptionDumperFixture,
                 "CaptionDumper rejects a PMT PID that collides with an always-demuxed "
                 "section PID") {
    auto& demux = SectionDemux();

    FeedPat(0, kSid, kPmtPid);

    REQUIRE(demux.hasPID(kPmtPid));

    FeedPat(1, kSid, ts::PID_EIT);

    CHECK(demux.hasPID(kPmtPid));
    CHECK(demux.hasPID(ts::PID_EIT));

    const auto& records = sink.Records();
    REQUIRE(records.size() == 1);
    REQUIRE(std::holds_alternative<aribcap_dump::DiagnosticRecord>(records[0]));

    const auto& diagnostic = std::get<aribcap_dump::DiagnosticRecord>(records[0]);
    REQUIRE(std::holds_alternative<aribcap_dump::ReservedPmtPidCollision>(diagnostic.kind));
    CHECK(std::get<aribcap_dump::ReservedPmtPidCollision>(diagnostic.kind).pid ==
          static_cast<std::uint16_t>(ts::PID_EIT));

    FeedPat(2, kSid, kSecondPmtPid);

    CHECK(demux.hasPID(kSecondPmtPid));
    CHECK_FALSE(demux.hasPID(kPmtPid));
    CHECK(demux.hasPID(ts::PID_EIT));
    CHECK(sink.Records().size() == 1);
}

TEST_CASE_METHOD(CaptionDumperFixture,
                 "CaptionDumper stays inert when the PAT never lists the target service") {
    auto& section_demux = SectionDemux();

    FeedPat(0, kSid + 1, kPmtPid);
    FeedPat(1, kSid + 1, kSecondPmtPid);

    CHECK_FALSE(section_demux.hasPID(kPmtPid));
    CHECK_FALSE(section_demux.hasPID(kSecondPmtPid));
    CHECK(sink.Records().empty());
}

// -------------------------------------------------------------------------------------------------
// PAT update tests, with a caption service already tracked
// -------------------------------------------------------------------------------------------------

TEST_CASE_METHOD(CaptionDumperFixture,
                 "CaptionDumper reacts to a PAT update while tracking a caption service") {
    auto& section_demux = SectionDemux();
    auto& pes_demux = PesDemux();

    FeedPat(0, kSid, kPmtPid);

    ts::PMT pmt(0, true, kSid, kPcrPid);
    AddCaptionStream(context, pmt, kCaptionPid);
    FeedPmt(pmt, kPmtPid);

    REQUIRE(section_demux.hasPID(kPmtPid));
    REQUIRE(pes_demux.hasPID(kCaptionPid));

    SECTION("a PMT PID switch clears the old PMT and caption PIDs") {
        FeedPat(1, kSid, kSecondPmtPid);

        CHECK_FALSE(section_demux.hasPID(kPmtPid));
        CHECK(section_demux.hasPID(kSecondPmtPid));
        CHECK_FALSE(pes_demux.hasPID(kCaptionPid));
    }

    SECTION("the target service leaving the PAT tears down PMT and caption tracking") {
        // A later, fully-valid PAT no longer lists `kSid` at all (a different,
        // unrelated service takes its place).
        FeedPat(1, kSid + 1, kSecondPmtPid);

        CHECK_FALSE(section_demux.hasPID(kPmtPid));
        CHECK_FALSE(pes_demux.hasPID(kCaptionPid));
    }

    SECTION("a version bump with an unchanged PMT PID keeps caption state") {
        FeedPat(1, kSid, kPmtPid);

        CHECK(section_demux.hasPID(kPmtPid));
        CHECK(pes_demux.hasPID(kCaptionPid));
    }
}

// -------------------------------------------------------------------------------------------------
// PMT caption stream synchronization tests
// -------------------------------------------------------------------------------------------------

TEST_CASE_METHOD(CaptionDumperFixture,
                 "CaptionDumper synchronizes caption PES PIDs from PMT streams") {
    auto& pes_demux = PesDemux();

    FeedPat(0, kSid, kPmtPid);

    ts::PMT first_pmt(0, true, kSid, kPcrPid);
    AddCaptionStream(context, first_pmt, kCaptionPid);
    FeedPmt(first_pmt, kPmtPid);

    REQUIRE(pes_demux.hasPID(kCaptionPid));
    CHECK_FALSE(pes_demux.hasPID(kSecondCaptionPid));

    ts::PMT second_pmt(1, true, kSid, kPcrPid);
    AddCaptionStream(context, second_pmt, kSecondCaptionPid);
    FeedPmt(second_pmt, kPmtPid);

    CHECK_FALSE(pes_demux.hasPID(kCaptionPid));
    REQUIRE(pes_demux.hasPID(kSecondCaptionPid));

    ts::PMT third_pmt(2, true, kSid, kPcrPid);
    third_pmt.streams[kVideoPid].stream_type = ts::ST_MPEG2_VIDEO;
    FeedPmt(third_pmt, kPmtPid);

    CHECK_FALSE(pes_demux.hasPID(kCaptionPid));
    CHECK_FALSE(pes_demux.hasPID(kSecondCaptionPid));
    CHECK_FALSE(pes_demux.hasPID(kVideoPid));
}

TEST_CASE_METHOD(CaptionDumperFixture,
                 "CaptionDumper re-registers a caption stream whose classification changes") {
    auto& pes_demux = PesDemux();
    const auto& emitters = CaptionRecordEmitters();

    FeedPat(0, kSid, kPmtPid);

    ts::PMT caption_pmt(0, true, kSid, kPcrPid);
    AddCaptionStream(context, caption_pmt, kCaptionPid);
    FeedPmt(caption_pmt, kPmtPid);

    REQUIRE(emitters.at(kCaptionPid).Info().caption_type == aribcaption::CaptionType::kCaption);

    // Same ES PID, but the component tag now marks it as superimpose.
    ts::PMT superimpose_pmt(1, true, kSid, kPcrPid);
    AddSuperimposeStream(context, superimpose_pmt, kCaptionPid);
    FeedPmt(superimpose_pmt, kPmtPid);

    CHECK(pes_demux.hasPID(kCaptionPid));

    CHECK(emitters.at(kCaptionPid).Info().caption_type == aribcaption::CaptionType::kSuperimpose);
}

TEST_CASE_METHOD(
    CaptionDumperFixture,
    "CaptionDumper tracks caption and superimpose streams simultaneously and tears both down") {
    auto& pes_demux = PesDemux();

    FeedPat(0, kSid, kPmtPid);

    ts::PMT pmt(0, true, kSid, kPcrPid);
    AddCaptionStream(context, pmt, kCaptionPid);
    AddSuperimposeStream(context, pmt, kSecondCaptionPid);
    FeedPmt(pmt, kPmtPid);

    REQUIRE(pes_demux.hasPID(kCaptionPid));
    REQUIRE(pes_demux.hasPID(kSecondCaptionPid));

    FeedPat(1, kSid + 1, kSecondPmtPid);

    CHECK_FALSE(pes_demux.hasPID(kCaptionPid));
    CHECK_FALSE(pes_demux.hasPID(kSecondCaptionPid));
    CHECK(CaptionRecordEmitters().empty());
}

TEST_CASE_METHOD(CaptionDumperFixture,
                 "CaptionDumper ignores a PMT whose service id is not the target service") {
    auto& pes_demux = PesDemux();

    FeedPat(0, kSid, kPmtPid);

    ts::PMT pmt(0, true, kSid + 1, kPcrPid);
    AddCaptionStream(context, pmt, kCaptionPid);
    FeedPmt(pmt, kPmtPid);

    CHECK_FALSE(pes_demux.hasPID(kCaptionPid));
    CHECK(CaptionRecordEmitters().empty());
}

// -------------------------------------------------------------------------------------------------
// EIT handling tests
// -------------------------------------------------------------------------------------------------

TEST_CASE_METHOD(CaptionDumperFixture,
                 "CaptionDumper filters EIT present/following by service id") {
    FeedEit(MakePfEit(kSid + 1, 0));

    CHECK(sink.Records().empty());

    FeedEit(MakePfEit(kSid, 0));

    const auto& records = sink.Records();
    REQUIRE(records.size() == 1);
    CHECK(std::holds_alternative<aribcap_dump::EitRecord>(records.front()));
    CHECK(std::get<aribcap_dump::EitRecord>(records.front()).section ==
          aribcap_dump::EitSection::kPresent);
}

TEST_CASE_METHOD(CaptionDumperFixture,
                 "CaptionDumper does not emit following when the present section is empty") {
    FeedEitTable(MakePfEitWithEmptyPresentSection(context, kSid, 0x1235, 0));

    CHECK(sink.Records().empty());
}

// -------------------------------------------------------------------------------------------------
// TOT handling tests
// -------------------------------------------------------------------------------------------------

TEST_CASE_METHOD(CaptionDumperFixture,
                 "CaptionDumper forwards a demuxed TOT to ProgramClock, updating its PCR/TOT "
                 "reference") {
    constexpr std::int64_t kPcr90k = 900'000;
    constexpr std::int64_t kUnixMs = 1'700'000'000'000;

    FeedPat(0, kSid, kPmtPid);

    ts::PMT pmt(0, true, kSid, kPcrPid);
    FeedPmt(pmt, kPmtPid);

    FeedPcr(kPcrPid, kPcr90k);

    auto& clock = ProgramClock();
    CHECK_FALSE(clock.PtsToUnixMs(kPcr90k).has_value());

    FeedTot(kUnixMs);

    const auto result = clock.PtsToUnixMs(kPcr90k);
    REQUIRE(result.has_value());
    CHECK(*result == kUnixMs);
}
