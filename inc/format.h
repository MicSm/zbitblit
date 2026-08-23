#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace zbb {

// On-disk archive format facts. Everything a reader or writer of the .zbb
// container must agree on lives here; changing any value breaks old archives.

inline constexpr std::array<std::uint8_t, 12> k_archive_magic{
    0x55, 0x2e, 0x3d, 0xa5, 43, 54, 34, 72, 11, 22, 15, 65};

/* stored file name: at most this many bytes, then a NUL terminator */
inline constexpr std::size_t k_max_stored_name = 255;

inline constexpr std::uint32_t k_block_unit_bytes = 100u * 1024u;
inline constexpr std::uint32_t k_lzp_len_threshold = 16;
inline constexpr std::uint32_t k_bwt_len_threshold = 8;

/* flag model: block kinds 0/1, raw literals 0..255, terminator 256 */
inline constexpr std::int16_t k_flag_symbols = 257;
/* mtf model: zero-run bits 0/1, ranks shifted to 2..256, terminator 257 */
inline constexpr std::int16_t k_mtf_symbols = 258;
inline constexpr std::int16_t k_raw_terminator = 256;
inline constexpr std::int16_t k_mtf_terminator = 257;

struct ArchiveHeader
{
    std::string name; /* name of the compressed file, at most k_max_stored_name bytes */
    std::uint32_t uncompressed_len = 0;
    std::uint8_t system_flag = 0; /* bit7 = LZP on/off; bits0-6 = block-size code */
};

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

} // namespace zbb
