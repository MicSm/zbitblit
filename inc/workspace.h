#pragma once

#include "inc/arithm.h"
#include "inc/bwt.h"
#include "inc/lzp_prep.h"
#include "inc/mio.h"
#include "inc/mtf.h"
#include "inc/status.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace zbb {

inline constexpr std::uint32_t k_lzp_len_threshold = 16;
inline constexpr std::uint32_t k_bwt_len_threshold = 8;
inline constexpr std::uint32_t k_block_unit_bytes = 100u * 1024u;

[[nodiscard]] constexpr std::uint8_t pack_system_flag(bool preprocess, std::uint8_t block_code)
{
    return static_cast<std::uint8_t>((preprocess ? 0x80u : 0u) | block_code);
}

[[nodiscard]] constexpr bool system_flag_preprocess(std::uint8_t flag)
{
    return (flag & 0x80u) != 0;
}

[[nodiscard]] constexpr std::uint8_t system_flag_block_code(std::uint8_t flag)
{
    return static_cast<std::uint8_t>(flag & 0x7fu);
}

[[nodiscard]] constexpr std::uint32_t block_bytes(std::uint8_t code)
{
    return static_cast<std::uint32_t>(code) * k_block_unit_bytes;
}

[[nodiscard]] constexpr bool is_block_code(std::uint8_t code)
{
    return code >= 1 && code <= 127;
}

struct DecodeOutcome
{
    // boffin: kept reconstructed pointer and length as one decode result
    ProcessError error = ProcessError::none;
    std::uint8_t* data = nullptr;
    std::uint32_t length = 0;
};

struct BlockWorkspace
{
    bool preprocess = false;
    LzpTables lzp;
    BwtWorkspace bwt;
    std::vector<std::uint8_t> front;
    std::vector<std::uint8_t> back;
    std::vector<std::uint32_t> idxs;
    ArithCoder arith{};
    MtfState mtf{};

    void acquire(bool preprocess_flag, std::uint32_t block_size)
    {
        // boffin: kept LZP and BWT tables on this per-file workspace instead of process globals
        preprocess = preprocess_flag;
        if (preprocess)
        {
            lzp.ensure();
        }
        bwt.ensure();

        const std::size_t buf_len = static_cast<std::size_t>(block_size) * 2u;
        front.resize(buf_len);
        back.resize(buf_len);
        idxs.resize(buf_len);
        arith.setup_models();
    }

    void begin_encode()
    {
        // boffin: kept encode start and decode start as distinct workspace phases
        arith.start_encode();
        mtf.setup();
    }

    void begin_decode(BitFile& in)
    {
        arith.start_decode(in);
        mtf.setup_decode();
    }

    [[nodiscard]] std::uint8_t* alternate(const std::uint8_t* cursor)
    {
        return (cursor == front.data()) ? back.data() : front.data();
    }

    void encode_payload(std::span<std::uint8_t> raw, BitFile& out);
    [[nodiscard]] DecodeOutcome decode_transformed(BitFile& in);
    [[nodiscard]] ProcessError finish_decoded(std::uint32_t expected_len, DecodeOutcome& decoded);
};

} // namespace zbb
