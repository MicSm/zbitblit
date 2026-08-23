#pragma once

#include "inc/mio.h"
#include "inc/workspace.h"

#include <cstdint>
#include <span>

namespace zbb {

void encode_payload_block(std::span<std::uint8_t> raw, BlockWorkspace& ws, bfile* out, bool preprocess);

[[nodiscard]] ProcessError decode_transformed_block(
    bfile* in,
    BlockWorkspace& ws,
    std::uint8_t*& decoded,
    std::uint32_t& len_out);

[[nodiscard]] ProcessError finish_decoded_block(
    bool preprocess,
    std::uint32_t expected_len,
    std::uint8_t*& ready,
    std::uint32_t& len_out,
    BlockWorkspace& ws);

} // namespace zbb
