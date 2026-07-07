// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

#include "tsduck/tsTSPacket.h"

namespace aribcap_dump {

// Callback invoked for each complete TS packet.
// Returning false stops delivery.
using PacketHandler = std::function<bool(const ts::TSPacket& packet)>;

// Source of raw bytes for `PacketFramer::FeedUntilEof`.
class ByteReader {
   public:
    virtual ~ByteReader() = default;

    // Reads up to `count` bytes into `buf`.
    // Returns a positive count of bytes read; returns 0 at EOF and a negative value on error.
    [[nodiscard]] virtual std::ptrdiff_t Read(std::uint8_t* buf, std::size_t count) = 0;
};

// Number of consecutive packets that must start with a sync byte before the
// framer stops searching for sync and continues in `kSynced` mode.
inline constexpr std::size_t kSyncProbePackets = 4;

// Byte-stream framer that reassembles aligned, synchronized TS packets.
class PacketFramer {
   public:
    // Frames TS packets and invokes `on_packet` once for each complete packet.
    // At the stream start it requires a valid initial sync; after a mid-stream
    // sync loss it resynchronizes by scanning for the next sync byte.
    //
    // Returns whether framing can continue:
    //
    //  - true: the framer consumed the input and can accept more.
    //  - false: delivery must stop because `on_packet` returned false, or because
    //    initial sync failed.
    [[nodiscard]] bool Feed(const std::uint8_t* data, std::size_t size,
                            const PacketHandler& on_packet);
    // Feeds bytes from `reader` until EOF.
    //
    // Returns whether framing ran to completion:
    //
    //  - true: the input reached EOF with sync established and every packet delivered.
    //  - false: `reader` failed, `on_packet` stopped delivery, or initial sync
    //    failed, including EOF before sync could be confirmed.
    [[nodiscard]] bool FeedUntilEof(ByteReader& reader, const PacketHandler& on_packet);

    // Returns true once initial sync has definitively failed.
    // Lets callers distinguish "no sync" from other feed/read failures.
    //
    // Returns false while sync is still pending or after sync has succeeded.
    [[nodiscard]] bool HasInitialSyncFailed() const {
        return initial_sync_failed_;
    }

   private:
    // Current framing state: initial sync, mid-stream resync, or packet delivery.
    enum class Mode { kInitialSync, kSeekingSync, kSynced };
    // Result of processing the current `Feed` chunk.
    enum class FeedResult { kContinue, kNeedMoreData, kStop };

    // Finds a run of 4 consecutive TS packets, each starting with a sync byte, emits
    // those packets, then stops searching for sync and continues in `kSynced` mode.
    // The first sync byte in the run defines the packet alignment.
    //
    // The search range depends on `initial_sync`:
    //
    //  - true: the search is bounded to the first `ts::PKT_SIZE` bytes. When no
    //    candidate in that span leads to a confirmed sync, the framer stops
    //    searching and sets `initial_sync_failed_` to true.
    //  - false: the scan continues forward without limit.
    [[nodiscard]] FeedResult FeedSeekingSync(const std::uint8_t*& cursor, const std::uint8_t* end,
                                             const PacketHandler& on_packet, bool initial_sync);
    // Emits complete packets while already synchronized.
    //
    // If a packet was split across `Feed` calls, this first completes and emits the
    // buffered packet. If the next byte is not a sync byte, it switches back to
    // `kSeekingSync` mode so the framer can resynchronize.
    [[nodiscard]] FeedResult FeedSynced(const std::uint8_t*& cursor, const std::uint8_t* end,
                                        const PacketHandler& on_packet);
    // Views `data` as one TS packet and delivers it to `on_packet`.
    [[nodiscard]] bool EmitPacket(const std::uint8_t* data, const PacketHandler& on_packet) const;
    // Emits the probe packets after sync has been confirmed.
    [[nodiscard]] bool EmitSyncConfirmedPackets(const std::uint8_t* data,
                                                const PacketHandler& on_packet) const;
    // Returns true if every packet boundary buffered so far in `pending_` holds a
    // sync byte.
    [[nodiscard]] bool PendingSyncBytesAreValid() const;
    // Discards the false sync candidate at `pending_[0]` and everything up to the
    // next sync byte in `pending_`, then shifts that sync byte to the front.
    //
    // Returns the number of bytes discarded.
    [[nodiscard]] std::size_t DiscardPendingUntilNextSync();

    // Called by `FeedUntilEof` once the reader reports EOF.
    // Sets `initial_sync_failed_` to true if sync was never established.
    void FinishAtEof();

    // Bytes reassembled across `Feed` calls. What this buffer holds depends on
    // `mode_`:
    //
    //  - `kInitialSync` / `kSeekingSync`: a sync probe window of up to 4 packets.
    //  - `kSynced`: a single packet split across a call boundary.
    //
    // While `mode_` is `kInitialSync` or `kSeekingSync`, buffering always starts at
    // a sync-byte candidate. Confirming the probe therefore never needs more than
    // 4 packets.
    std::array<std::uint8_t, ts::PKT_SIZE * kSyncProbePackets> pending_{};
    // Number of valid bytes in `pending_`.
    std::size_t pending_size_ = 0;

    // Number of stream bytes discarded so far during `kInitialSync`.
    //
    // When `pending_` holds a sync-byte candidate, this is the stream offset of
    // that candidate at `pending_[0]`. When `pending_` is empty, this is the offset
    // where the next scan will resume.
    //
    // Initial sync fails once this offset reaches `ts::PKT_SIZE`; `FeedSeekingSync`
    // explains the bound.
    std::size_t initial_offset_ = 0;
    Mode mode_ = Mode::kInitialSync;
    bool initial_sync_failed_ = false;
};

// Result of reading and framing a whole TS input.
enum class ReadFrameStatus { kOk, kFailed, kNoSync };

// Reads raw bytes from `fd` via read(2) until EOF, feeding packets to `on_packet`.
[[nodiscard]] ReadFrameStatus ReadAndFramePackets(int fd, const PacketHandler& on_packet);

}  // namespace aribcap_dump
