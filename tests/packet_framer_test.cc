// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/packet_framer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace {

using aribcap_dump::kSyncProbePackets;

std::array<std::uint8_t, ts::PKT_SIZE> MakePacket(std::uint8_t marker) {
    std::array<std::uint8_t, ts::PKT_SIZE> packet{};
    packet.fill(0xFF);
    packet[0] = 0x47;
    packet[1] = marker;
    return packet;
}

void AppendPacket(std::vector<std::uint8_t>& bytes, std::uint8_t marker) {
    const auto packet = MakePacket(marker);
    bytes.insert(bytes.end(), packet.begin(), packet.end());
}

void AppendPackets(std::vector<std::uint8_t>& bytes,
                   const std::array<std::uint8_t, kSyncProbePackets>& markers) {
    for (const std::uint8_t marker : markers) {
        AppendPacket(bytes, marker);
    }
}

class MemoryByteReader : public aribcap_dump::ByteReader {
   public:
    MemoryByteReader(std::vector<std::uint8_t> data, std::size_t chunk_size)
        : data_(std::move(data)), chunk_size_(chunk_size) {}

    [[nodiscard]] std::ptrdiff_t Read(std::uint8_t* buf, std::size_t count) override {
        ++read_count_;
        const std::size_t remaining = data_.size() - pos_;
        const std::size_t size = std::min({chunk_size_, count, remaining});
        if (size > 0) {
            std::memcpy(buf, data_.data() + pos_, size);
            pos_ += size;
        }
        return static_cast<std::ptrdiff_t>(size);
    }

    std::size_t read_count() const {
        return read_count_;
    }

   private:
    std::vector<std::uint8_t> data_;
    std::size_t chunk_size_;
    std::size_t pos_ = 0;
    std::size_t read_count_ = 0;
};

class FailingByteReader : public aribcap_dump::ByteReader {
   public:
    [[nodiscard]] std::ptrdiff_t Read(std::uint8_t*, std::size_t) override {
        return -1;
    }
};

class PacketFramerFixture {
   public:
    aribcap_dump::PacketFramer framer;
    std::vector<std::uint8_t> markers;

    [[nodiscard]] bool Feed(const std::vector<std::uint8_t>& bytes) {
        return FeedRaw(bytes.data(), bytes.size());
    }

    template <std::size_t N>
    [[nodiscard]] bool Feed(const std::array<std::uint8_t, N>& bytes) {
        return FeedRaw(bytes.data(), bytes.size());
    }

    [[nodiscard]] bool Feed(std::uint8_t byte) {
        return FeedRaw(&byte, 1);
    }

    [[nodiscard]] bool FeedUntilEof(aribcap_dump::ByteReader& reader) {
        return framer.FeedUntilEof(reader, [&](const ts::TSPacket& packet) {
            markers.push_back(packet.b[1]);
            return true;
        });
    }

   private:
    [[nodiscard]] bool FeedRaw(const std::uint8_t* data, std::size_t size) {
        return framer.Feed(data, size, [&](const ts::TSPacket& packet) {
            markers.push_back(packet.b[1]);
            return true;
        });
    }
};

}  // namespace

// -------------------------------------------------------------------------------------------------
// Initial sync tests
// -------------------------------------------------------------------------------------------------

TEST_CASE_METHOD(PacketFramerFixture, "PacketFramer aligns TS packets after garbage") {
    std::vector<std::uint8_t> bytes = {0x00, 0x01, 0x02};
    AppendPackets(bytes, {0x10, 0x20, 0x30, 0x40});

    CHECK(Feed(bytes));

    REQUIRE(markers.size() == 4);
    CHECK(markers[0] == 0x10);
    CHECK(markers[1] == 0x20);
    CHECK(markers[2] == 0x30);
    CHECK(markers[3] == 0x40);
}

TEST_CASE_METHOD(PacketFramerFixture,
                 "PacketFramer scans every sync-byte candidate in the first packet") {
    // A stray sync byte at offset 0 whose probe misaligns, then the real,
    // aligned packet run beginning within the first packet-length.
    std::vector<std::uint8_t> bytes = {0x47, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    AppendPackets(bytes, {0x10, 0x20, 0x30, 0x40});

    CHECK(Feed(bytes));

    CHECK_FALSE(framer.HasInitialSyncFailed());
    REQUIRE(markers.size() == 4);
    CHECK(markers[0] == 0x10);
    CHECK(markers[1] == 0x20);
    CHECK(markers[2] == 0x30);
    CHECK(markers[3] == 0x40);
}

TEST_CASE_METHOD(PacketFramerFixture,
                 "PacketFramer rejects input whose packets start beyond the first packet") {
    std::vector<std::uint8_t> bytes(ts::PKT_SIZE, 0xFF);
    bytes[0] = 0x47;
    bytes[1] = 0x99;
    bytes.push_back(0x00);
    AppendPackets(bytes, {0x10, 0x20, 0x30, 0x40});

    CHECK_FALSE(Feed(bytes));

    CHECK(markers.empty());
    CHECK(framer.HasInitialSyncFailed());
}

TEST_CASE_METHOD(PacketFramerFixture,
                 "PacketFramer rejects input with no sync byte in the first packet") {
    const std::vector<std::uint8_t> bytes(ts::PKT_SIZE + 32, 0x00);

    CHECK_FALSE(Feed(bytes));

    CHECK(framer.HasInitialSyncFailed());
}

TEST_CASE_METHOD(PacketFramerFixture,
                 "PacketFramer locks initial sync when leading garbage spans Feed calls") {
    // Leading garbage delivered one byte per call keeps the framer seeking without a
    // candidate; the skipped-byte count (`initial_offset_`) must persist across
    // `Feed` calls.
    const std::array<std::uint8_t, 3> garbage = {0x00, 0x01, 0x02};
    for (const std::uint8_t byte : garbage) {
        CHECK(Feed(byte));
    }

    std::vector<std::uint8_t> packets;
    AppendPackets(packets, {0x10, 0x20, 0x30, 0x40});
    CHECK(Feed(packets));

    CHECK_FALSE(framer.HasInitialSyncFailed());
    REQUIRE(markers.size() == 4);
    CHECK(markers[0] == 0x10);
    CHECK(markers[3] == 0x40);
}

TEST_CASE_METHOD(PacketFramerFixture, "PacketFramer waits for four packets before initial sync") {
    const auto packet_count = GENERATE(std::size_t{1}, std::size_t{2}, std::size_t{3});

    std::vector<std::uint8_t> bytes;
    for (std::size_t index = 0; index < packet_count; ++index) {
        AppendPacket(bytes, static_cast<std::uint8_t>(0x10 + index));
    }

    CHECK(Feed(bytes));
    CHECK(markers.empty());
}

// -------------------------------------------------------------------------------------------------
// Packet delivery tests
// -------------------------------------------------------------------------------------------------

TEST_CASE_METHOD(PacketFramerFixture,
                 "PacketFramer keeps complete packet before trailing partial data") {
    std::vector<std::uint8_t> bytes;
    AppendPackets(bytes, {0x10, 0x20, 0x30, 0x40});
    AppendPacket(bytes, 0x50);
    bytes.insert(bytes.end(), {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06});

    CHECK(Feed(bytes));

    REQUIRE(markers.size() == 5);
    CHECK(markers[0] == 0x10);
    CHECK(markers[1] == 0x20);
    CHECK(markers[2] == 0x30);
    CHECK(markers[3] == 0x40);
    CHECK(markers[4] == 0x50);
}

TEST_CASE_METHOD(PacketFramerFixture,
                 "PacketFramer emits one packet immediately after sync is locked") {
    std::vector<std::uint8_t> first_feed;
    AppendPackets(first_feed, {0x10, 0x20, 0x30, 0x40});

    const auto single_packet = MakePacket(0x50);

    CHECK(Feed(first_feed));
    CHECK(Feed(single_packet));

    REQUIRE(markers.size() == 5);
    CHECK(markers[4] == 0x50);
}

// -------------------------------------------------------------------------------------------------
// Resync tests
// -------------------------------------------------------------------------------------------------

TEST_CASE_METHOD(PacketFramerFixture,
                 "PacketFramer requires four packets to resync after sync loss") {
    std::vector<std::uint8_t> first_feed;
    AppendPackets(first_feed, {0x10, 0x20, 0x30, 0x40});

    std::vector<std::uint8_t> lost_sync(ts::PKT_SIZE, 0xFF);
    lost_sync[0] = 0x00;
    AppendPackets(lost_sync, {0x50, 0x60, 0x70, 0x80});

    CHECK(Feed(first_feed));
    CHECK(Feed(lost_sync));

    REQUIRE(markers.size() == 8);
    CHECK(markers[0] == 0x10);
    CHECK(markers[3] == 0x40);
    CHECK(markers[4] == 0x50);
    CHECK(markers[7] == 0x80);
}

TEST_CASE_METHOD(PacketFramerFixture,
                 "PacketFramer does not resync with fewer than four packets after sync loss") {
    std::vector<std::uint8_t> first_feed;
    AppendPackets(first_feed, {0x10, 0x20, 0x30, 0x40});

    std::vector<std::uint8_t> lost_sync(ts::PKT_SIZE, 0xFF);
    lost_sync[0] = 0x00;
    AppendPacket(lost_sync, 0x50);
    AppendPacket(lost_sync, 0x60);
    AppendPacket(lost_sync, 0x70);

    CHECK(Feed(first_feed));
    CHECK(Feed(lost_sync));

    REQUIRE(markers.size() == 4);
    CHECK(markers[0] == 0x10);
    CHECK(markers[3] == 0x40);
}

// -------------------------------------------------------------------------------------------------
// Callback and reader tests
// -------------------------------------------------------------------------------------------------

TEST_CASE_METHOD(PacketFramerFixture, "PacketFramer stops when callback asks") {
    std::vector<std::uint8_t> bytes;
    AppendPackets(bytes, {0x10, 0x20, 0x30, 0x40});

    CHECK_FALSE(framer.Feed(bytes.data(), bytes.size(), [](const ts::TSPacket&) { return false; }));
}

TEST_CASE_METHOD(PacketFramerFixture,
                 "PacketFramer FeedUntilEof reassembles packets split across short reads") {
    std::vector<std::uint8_t> bytes;
    AppendPackets(bytes, {0x10, 0x20, 0x30, 0x40});

    MemoryByteReader reader(std::move(bytes), 17);

    CHECK(FeedUntilEof(reader));

    REQUIRE(markers.size() == 4);
    CHECK(markers[0] == 0x10);
    CHECK(markers[1] == 0x20);
    CHECK(markers[2] == 0x30);
    CHECK(markers[3] == 0x40);
}

TEST_CASE_METHOD(PacketFramerFixture,
                 "PacketFramer FeedUntilEof stops after callback returns false") {
    std::vector<std::uint8_t> bytes;
    AppendPackets(bytes, {0x10, 0x20, 0x30, 0x40});

    MemoryByteReader reader(std::move(bytes), ts::PKT_SIZE * kSyncProbePackets);
    std::size_t callback_count = 0;

    CHECK_FALSE(framer.FeedUntilEof(reader, [&](const ts::TSPacket&) {
        ++callback_count;
        return false;
    }));

    CHECK(callback_count == 1);
    CHECK(reader.read_count() == 1);
}

TEST_CASE_METHOD(PacketFramerFixture, "PacketFramer reports no sync on empty input") {
    MemoryByteReader reader({}, ts::PKT_SIZE);
    std::size_t callback_count = 0;

    CHECK_FALSE(framer.FeedUntilEof(reader, [&](const ts::TSPacket&) {
        ++callback_count;
        return true;
    }));

    CHECK(callback_count == 0);
    CHECK(reader.read_count() == 1);
    CHECK(framer.HasInitialSyncFailed());
}

TEST_CASE_METHOD(PacketFramerFixture,
                 "PacketFramer reports no sync when input ends before sync is confirmed") {
    std::vector<std::uint8_t> bytes;
    AppendPackets(bytes, {0x10, 0x20, 0x30, 0x40});
    bytes.resize(bytes.size() - 1);  // one byte short of a confirmable probe window

    MemoryByteReader reader(std::move(bytes), ts::PKT_SIZE);
    std::size_t callback_count = 0;

    CHECK_FALSE(framer.FeedUntilEof(reader, [&](const ts::TSPacket&) {
        ++callback_count;
        return true;
    }));

    CHECK(callback_count == 0);
    CHECK(framer.HasInitialSyncFailed());
}

TEST_CASE_METHOD(PacketFramerFixture, "PacketFramer FeedUntilEof returns false on reader error") {
    FailingByteReader reader;
    std::size_t callback_count = 0;

    CHECK_FALSE(framer.FeedUntilEof(reader, [&](const ts::TSPacket&) {
        ++callback_count;
        return true;
    }));

    CHECK(callback_count == 0);
}
