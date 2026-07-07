// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <optional>

#include "tsduck/tsDataComponentDescriptor.h"
#include "tsduck/tsDuckContext.h"
#include "tsduck/tsPMT.h"
#include "tsduck/tsStreamIdentifierDescriptor.h"
#include "tsduck/tsStreamType.h"

namespace aribcap_dump::test {

// Adds a private-PES PMT stream entry at `es_pid`, with an optional stream_identifier
// component_tag and an optional data_component_id descriptor.
inline ts::PMT::Stream& AddPrivatePesStream(ts::DuckContext& context, ts::PMT& pmt, ts::PID es_pid,
                                            std::optional<std::uint8_t> component_tag,
                                            std::optional<std::uint16_t> data_component_id) {
    auto& stream = pmt.streams[es_pid];
    stream.stream_type = ts::ST_PES_PRIV;

    if (component_tag.has_value()) {
        ts::StreamIdentifierDescriptor descriptor;
        descriptor.component_tag = *component_tag;
        stream.descs.add(context, descriptor);
    }

    if (data_component_id.has_value()) {
        ts::DataComponentDescriptor descriptor;
        descriptor.data_component_id = *data_component_id;
        stream.descs.add(context, descriptor);
    }

    return stream;
}

}  // namespace aribcap_dump::test
