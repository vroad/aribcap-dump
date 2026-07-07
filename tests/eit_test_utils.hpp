// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>

#include "tsduck/tsEIT.h"

namespace aribcap_dump::test {

// Builds an actual-TS present/following ts::EIT with no events, the shape ParseEit/HandleEit
// expect as their starting point.
inline ts::EIT MakePfEit(std::uint16_t service_id, std::uint8_t version,
                         std::uint16_t transport_stream_id = 0,
                         std::uint16_t original_network_id = 0) {
    return ts::EIT(/*is_actual=*/true, /*is_pf=*/true, /*eits_index=*/0, version,
                   /*is_current=*/true, service_id, transport_stream_id, original_network_id);
}

}  // namespace aribcap_dump::test
