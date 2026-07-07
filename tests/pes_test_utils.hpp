// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

#include "tsduck/tsPESPacket.h"
#include "tsduck/tsTSPacket.h"

#include <catch2/catch_test_macros.hpp>

namespace aribcap_dump::test {

constexpr ts::PID kCaptionPid = 0x123;

// Builds a valid private_stream_1 PES packet wrapping `payload`, optionally
// carrying `pts`.
//
// The PTS bit layout is encoded by TSDuck's TSPacket::setPTS
// rather than by hand; the test therefore shares no PTS logic with the code
// under test.
inline ts::PESPacket MakePesPacket(const std::vector<std::uint8_t>& payload,
                                   std::optional<std::uint64_t> pts = std::nullopt) {
    std::vector<std::uint8_t> bytes = {
        0x00, 0x00, 0x01,  // packet_start_code_prefix: fixed 24-bit 0x000001
        0xBD,        // stream_id: MPEG assigns 0xBD to private_stream_1, used by ARIB captions
        0x00, 0x00,  // PES_packet_length placeholder; overwritten once payload is appended
        0x80,  // PES header flags: leading '10' is required by MPEG PES syntax; other flags are set
               // to 0
    };

    if (pts.has_value()) {
        bytes.push_back(0x80);  // PTS_DTS_flags: PTS only, no DTS
        bytes.push_back(0x05);  // PES_header_data_length: 5 PTS bytes
        bytes.insert(bytes.end(),
                     {0x21, 0x00, 0x01, 0x00, 0x01});  // Valid PTS field for value 0, including the
                                                       // marker bits required by PES syntax.

        // setPTS() overwrites an existing PTS field; it does not create one.
        // The initial PTS bytes must be valid before setPTS() can rewrite them.
        ts::TSPacket ts_packet;
        ts_packet.init();
        ts_packet.setPUSI();
        std::copy(bytes.begin(), bytes.end(), ts_packet.getPayload());
        ts_packet.setPTS(*pts);
        std::copy(ts_packet.getPayload() + 9, ts_packet.getPayload() + 14, bytes.begin() + 9);
    } else {
        bytes.push_back(0x00);  // PTS_DTS_flags: none
        bytes.push_back(0x00);  // PES_header_data_length: 0
    }

    bytes.insert(bytes.end(), payload.begin(), payload.end());

    const auto packet_length = static_cast<std::uint16_t>(bytes.size() - 6);
    bytes[4] = static_cast<std::uint8_t>(packet_length >> 8);
    bytes[5] = static_cast<std::uint8_t>(packet_length & 0xFF);

    ts::PESPacket packet(bytes.data(), bytes.size(), kCaptionPid);
    REQUIRE(packet.isValid());

    return packet;
}

}  // namespace aribcap_dump::test
