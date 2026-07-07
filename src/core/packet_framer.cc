// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/packet_framer.hpp"

#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>

namespace aribcap_dump {
namespace {

constexpr std::size_t kChunkSize = 64 * 1024;
constexpr std::size_t kSyncProbeSize = ts::PKT_SIZE * kSyncProbePackets;

// Production implementation of `ByteReader` that reads from a file descriptor via `read(2)`.
class FdByteReader : public ByteReader {
   public:
    explicit FdByteReader(int fd) : fd_(fd) {}

    [[nodiscard]] std::ptrdiff_t Read(std::uint8_t* buf, std::size_t count) override {
        ssize_t n;

        do {
            n = ::read(fd_, buf, count);
        } while (n < 0 && errno == EINTR);

        return static_cast<std::ptrdiff_t>(n);
    }

   private:
    int fd_;
};

}  // namespace

bool PacketFramer::Feed(const std::uint8_t* data, std::size_t size,
                        const PacketHandler& on_packet) {
    if (size == 0) {
        return true;
    }

    const std::uint8_t* cursor = data;
    const std::uint8_t* const end = data + size;

    while (cursor != end) {
        FeedResult result = FeedResult::kContinue;

        switch (mode_) {
            case Mode::kInitialSync:
                result = FeedSeekingSync(cursor, end, on_packet, /*initial_sync=*/true);
                break;
            case Mode::kSeekingSync:
                result = FeedSeekingSync(cursor, end, on_packet, /*initial_sync=*/false);
                break;
            case Mode::kSynced:
                result = FeedSynced(cursor, end, on_packet);
                break;
        }

        if (result == FeedResult::kStop) {
            return false;
        }

        if (result == FeedResult::kNeedMoreData) {
            return true;
        }
    }

    return true;
}

PacketFramer::FeedResult PacketFramer::FeedSeekingSync(const std::uint8_t*& cursor,
                                                       const std::uint8_t* end,
                                                       const PacketHandler& on_packet,
                                                       bool initial_sync) {
    // Buffer a probe window at the first sync-byte candidate and confirm 4
    // consecutive packets before switching to `kSynced` mode.
    //
    // During `initial_sync`, `initial_offset_` tracks the candidate's stream offset
    // (equivalently, the number of leading bytes skipped). The search is bounded to
    // the first `ts::PKT_SIZE` bytes. When no candidate in that span leads to a
    // confirmed sync, the framer stops searching and sets `initial_sync_failed_` to
    // true.
    //
    // Otherwise (mid-stream resync) the scan continues forward without limit.
    if (pending_size_ == 0) {
        const auto* const sync = std::find(cursor, end, ts::SYNC_BYTE);

        if (initial_sync) {
            initial_offset_ += static_cast<std::size_t>(sync - cursor);
        }

        cursor = sync;

        if (initial_sync && initial_offset_ >= ts::PKT_SIZE) {
            initial_sync_failed_ = true;

            return FeedResult::kStop;
        }

        if (sync == end) {
            return FeedResult::kNeedMoreData;
        }
    }

    const std::size_t needed = kSyncProbeSize - pending_size_;
    const std::size_t copied = std::min(needed, static_cast<std::size_t>(end - cursor));
    std::memcpy(pending_.data() + pending_size_, cursor, copied);
    pending_size_ += copied;
    cursor += copied;

    if (!PendingSyncBytesAreValid()) {
        const std::size_t discarded = DiscardPendingUntilNextSync();

        if (initial_sync) {
            // Enforce the initial-sync bound after skipping a false candidate:
            // initial sync fails once `initial_offset_` reaches `ts::PKT_SIZE`.
            initial_offset_ += discarded;

            if (initial_offset_ >= ts::PKT_SIZE) {
                initial_sync_failed_ = true;

                return FeedResult::kStop;
            }
        }

        return FeedResult::kContinue;
    }

    if (pending_size_ < kSyncProbeSize) {
        return FeedResult::kNeedMoreData;
    }

    if (!EmitSyncConfirmedPackets(pending_.data(), on_packet)) {
        pending_size_ = 0;

        return FeedResult::kStop;
    }

    pending_size_ = 0;
    mode_ = Mode::kSynced;

    return FeedResult::kContinue;
}

PacketFramer::FeedResult PacketFramer::FeedSynced(const std::uint8_t*& cursor,
                                                  const std::uint8_t* end,
                                                  const PacketHandler& on_packet) {
    if (pending_size_ != 0) {
        const std::size_t needed = ts::PKT_SIZE - pending_size_;
        const std::size_t copied = std::min(needed, static_cast<std::size_t>(end - cursor));
        std::memcpy(pending_.data() + pending_size_, cursor, copied);
        pending_size_ += copied;
        cursor += copied;

        if (pending_size_ < ts::PKT_SIZE) {
            return FeedResult::kNeedMoreData;
        }

        if (!EmitPacket(pending_.data(), on_packet)) {
            pending_size_ = 0;

            return FeedResult::kStop;
        }

        pending_size_ = 0;

        return FeedResult::kContinue;
    }

    if (*cursor != ts::SYNC_BYTE) {
        mode_ = Mode::kSeekingSync;

        return FeedResult::kContinue;
    }

    if (static_cast<std::size_t>(end - cursor) < ts::PKT_SIZE) {
        pending_size_ = static_cast<std::size_t>(end - cursor);
        std::memcpy(pending_.data(), cursor, pending_size_);

        return FeedResult::kNeedMoreData;
    }

    if (!EmitPacket(cursor, on_packet)) {
        return FeedResult::kStop;
    }

    cursor += ts::PKT_SIZE;

    return FeedResult::kContinue;
}

bool PacketFramer::FeedUntilEof(ByteReader& reader, const PacketHandler& on_packet) {
    std::array<std::uint8_t, kChunkSize> buffer;

    for (;;) {
        const std::ptrdiff_t count = reader.Read(buffer.data(), buffer.size());

        if (count < 0) {
            return false;
        }

        if (count == 0) {
            FinishAtEof();

            return !initial_sync_failed_;
        }

        if (!Feed(buffer.data(), static_cast<std::size_t>(count), on_packet)) {
            return false;
        }
    }
}

void PacketFramer::FinishAtEof() {
    // EOF while `mode_` is still `kInitialSync` means sync was never confirmed.
    // Set `initial_sync_failed_` to true.
    //
    // An unconfirmed sync could mean the input is MPEG-TS that was somehow truncated
    // before 4 consecutive sync bytes were observed, or that it is not MPEG-TS at
    // all; those two cases are indistinguishable here.
    if (mode_ == Mode::kInitialSync) {
        initial_sync_failed_ = true;
    }
}

bool PacketFramer::EmitPacket(const std::uint8_t* data, const PacketHandler& on_packet) const {
    // Reinterprets `data` in place as a `ts::TSPacket` and passes it to `on_packet`,
    // without copying.
    //
    // `ts::TSPacket` is a final class with a single `uint8_t b[PKT_SIZE]` member, no
    // constructor, and alignment 1. TSDuck documents that arrays of this class share the
    // physical layout of a transport stream, so this relies on that documented layout
    // guarantee, not on alignment alone.
    //
    // No `TSPacket` object is constructed here; every mainstream compiler honors this as a
    // strict-aliasing exception, but the C++ standard itself does not guarantee it.
    return on_packet(*reinterpret_cast<const ts::TSPacket*>(data));
}

bool PacketFramer::EmitSyncConfirmedPackets(const std::uint8_t* data,
                                            const PacketHandler& on_packet) const {
    for (std::size_t index = 0; index < kSyncProbePackets; ++index) {
        if (!EmitPacket(data + index * ts::PKT_SIZE, on_packet)) {
            return false;
        }
    }

    return true;
}

bool PacketFramer::PendingSyncBytesAreValid() const {
    for (std::size_t index = 0; index < kSyncProbePackets; ++index) {
        const std::size_t offset = index * ts::PKT_SIZE;

        if (pending_size_ > offset && pending_[offset] != ts::SYNC_BYTE) {
            return false;
        }
    }

    return true;
}

std::size_t PacketFramer::DiscardPendingUntilNextSync() {
    const auto* const begin = pending_.data();
    const auto* const end = begin + pending_size_;
    const auto* const sync = std::find(begin + 1, end, ts::SYNC_BYTE);
    const auto discarded = static_cast<std::size_t>(sync - begin);
    pending_size_ = static_cast<std::size_t>(end - sync);
    std::memmove(pending_.data(), sync, pending_size_);

    return discarded;
}

ReadFrameStatus ReadAndFramePackets(int fd, const PacketHandler& on_packet) {
    FdByteReader reader(fd);
    PacketFramer framer;

    const bool ok = framer.FeedUntilEof(reader, on_packet);

    if (framer.HasInitialSyncFailed()) {
        return ReadFrameStatus::kNoSync;
    }

    return ok ? ReadFrameStatus::kOk : ReadFrameStatus::kFailed;
}

}  // namespace aribcap_dump
