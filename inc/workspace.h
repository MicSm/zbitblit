#pragma once

#include "inc/arithm.h"
#include "inc/bwt.h"
#include "inc/format.h"
#include "inc/lzp_prep.h"
#include "inc/mio.h"
#include "inc/mtf.h"
#include "inc/status.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

namespace zbb {

static_assert(k_flag_symbols <= k_max_alphabet, "flag model must fit the coder tables");
static_assert(k_mtf_symbols <= k_max_alphabet, "mtf model must fit the coder tables");

struct BlockWorkspace
{
    bool preprocess = false;
    LzpTables lzp;
    BwtWorkspace bwt;
    std::vector<std::uint8_t> front;
    std::vector<std::uint8_t> back;
    std::vector<std::uint32_t> idxs;
    ArithEncoder encoder{};
    ArithDecoder decoder{};
    ArithCodingContext flag_model{};
    ArithCodingContext mtf_model{};
    MtfEncoder mtf_encoder{};
    MtfDecoder mtf_decoder{};

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
        flag_model.setup(k_flag_symbols);
        mtf_model.setup(k_mtf_symbols);
    }

    void begin_encode()
    {
        // boffin: kept encode start and decode start as distinct workspace phases
        encoder.start();
        mtf_encoder.reset();
    }

    void begin_decode(BitReader& in)
    {
        decoder.start(in);
        mtf_decoder.reset();
    }

    [[nodiscard]] std::uint8_t* alternate(const std::uint8_t* cursor)
    {
        return (cursor == front.data()) ? back.data() : front.data();
    }

    void encode_payload(std::span<std::uint8_t> raw, BitWriter& out);

    [[nodiscard]] std::expected<std::span<std::uint8_t>, ProcessError> decode_transformed(BitReader& in);

    [[nodiscard]] std::expected<std::span<const std::uint8_t>, ProcessError> finish_decoded(
        std::uint32_t expected_len,
        std::span<std::uint8_t> decoded);
};

} // namespace zbb
