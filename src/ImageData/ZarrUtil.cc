/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

// # ZarrUtil.cc: low-level Zarr v3 metadata and data helpers
#include "ZarrUtil.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include <blosc.h>
#include <spdlog/fmt/fmt.h>
#include <zlib.h>
#include <zstd.h>

namespace carta {

const nlohmann::json* FindJsonPtr(const nlohmann::json& obj, const char* ptr) {
    try {
        return &obj.at(nlohmann::json::json_pointer(ptr));
    } catch (...) {
        return nullptr;
    }
}

std::vector<std::string> ListZarrImageArrays(const std::string& path_string) {
    std::vector<std::string> array_names;
    std::error_code error_code;
    std::filesystem::path path(path_string);

    if (!std::filesystem::is_directory(path, error_code)) {
        return array_names;
    }

    if (!std::filesystem::exists(path / "zarr.json", error_code)) {
        return array_names;
    }

    for (const char* array_name : ZARR_SUPPORTED_IMAGE_ARRAYS) {
        std::filesystem::path array_json_path = path / array_name / "zarr.json";
        std::error_code exists_error;
        if (std::filesystem::exists(array_json_path, exists_error)) {
            array_names.emplace_back(array_name);
        }
    }

    return array_names;
}

bool IsSupportedZarrImage(const std::string& path_string) {
    return !ListZarrImageArrays(path_string).empty();
}

namespace {

constexpr uint32_t UNICODE_MAX = 0x10FFFF;
constexpr uint32_t SURROGATE_MIN = 0xD800;
constexpr uint32_t SURROGATE_MAX = 0xDFFF;

// Append a single Unicode code point to a UTF-8 string using the standard UTF-8 bit patterns.
void AppendUtf8(std::string& out, uint32_t code_point) {
    if (code_point > UNICODE_MAX || (code_point >= SURROGATE_MIN && code_point <= SURROGATE_MAX)) {
        throw std::runtime_error(fmt::format("Invalid Unicode code point: {:#x}", code_point));
    }
    if (code_point < 0x80) {
        // 1-byte sequence (ASCII): 0xxxxxxx
        out.push_back(static_cast<char>(code_point));
    } else if (code_point < 0x800) {
        // 2-byte sequence: 110xxxxx 10xxxxxx
        out.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else if (code_point < 0x10000) {
        // 3-byte sequence: 1110xxxx 10xxxxxx 10xxxxxx
        out.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else {
        // 4-byte sequence: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
        out.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
}

// Read a 32-bit unsigned integer from a 4-byte buffer with the given endianness.
uint32_t ReadUint32(const uint8_t* bytes, bool little_endian) {
    constexpr int shift_byte1 = 8;
    constexpr int shift_byte2 = 16;
    constexpr int shift_byte3 = 24;
    if (little_endian) {
        return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << shift_byte1) |
               (static_cast<uint32_t>(bytes[2]) << shift_byte2) | (static_cast<uint32_t>(bytes[3]) << shift_byte3);
    }
    return static_cast<uint32_t>(bytes[3]) | (static_cast<uint32_t>(bytes[2]) << shift_byte1) |
           (static_cast<uint32_t>(bytes[1]) << shift_byte2) | (static_cast<uint32_t>(bytes[0]) << shift_byte3);
}

// Decode a contiguous buffer of fixed-length UTF-32 strings into UTF-8 strings (null-padding stripped).
std::vector<std::string> DecodeFixedLengthUtf32(
    const std::vector<uint8_t>& bytes, size_t num_elements, size_t length_bytes, bool little_endian) {
    const size_t chars_per_element = length_bytes / sizeof(uint32_t);
    std::vector<std::string> result;
    result.reserve(num_elements);

    for (size_t element = 0; element < num_elements; ++element) {
        std::string value;
        for (size_t code_index = 0; code_index < chars_per_element; ++code_index) {
            const uint8_t* ptr = bytes.data() + (element * length_bytes) + (code_index * sizeof(uint32_t));
            uint32_t code_point = ReadUint32(ptr, little_endian);
            if (code_point == 0) {
                break; // trailing null padding
            }
            AppendUtf8(value, code_point);
        }
        result.push_back(std::move(value));
    }

    return result;
}

enum class BytesCodec {
    Blosc,
    Gzip,
    Zstd,
};

// Decompress a Zstandard frame into a byte buffer.
size_t ZstdDecompress(const std::vector<uint8_t>& compressed, std::vector<uint8_t>& decompressed) {
    const size_t decompressed_size = ZSTD_decompress(decompressed.data(), decompressed.size(), compressed.data(), compressed.size());
    if (ZSTD_isError(decompressed_size)) {
        throw std::runtime_error(std::string("Zstd decompression failed: ") + ZSTD_getErrorName(decompressed_size));
    }
    return decompressed_size;
}

size_t GzipDecompress(const std::vector<uint8_t>& compressed, std::vector<uint8_t>& decompressed) {
    if (compressed.size() > std::numeric_limits<uInt>::max()) {
        throw std::runtime_error(fmt::format("Gzip buffer is too large to decompress ({} bytes)", compressed.size()));
    }
    if (decompressed.size() > std::numeric_limits<uInt>::max()) {
        throw std::runtime_error(fmt::format("Gzip expected output is too large to decompress ({} bytes)", decompressed.size()));
    }

    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(compressed.data()));
    stream.avail_in = static_cast<uInt>(compressed.size());

    constexpr int gzip_window_bits = 16 + MAX_WBITS;
    int status = inflateInit2(&stream, gzip_window_bits);
    if (status != Z_OK) {
        throw std::runtime_error("Failed to initialize gzip decompression");
    }

    stream.next_out = reinterpret_cast<Bytef*>(decompressed.data());
    stream.avail_out = static_cast<uInt>(decompressed.size());

    status = inflate(&stream, Z_FINISH);
    const size_t decompressed_size = stream.total_out;
    inflateEnd(&stream);

    if (status != Z_STREAM_END) {
        if (status == Z_OK && stream.avail_out == 0) {
            throw std::runtime_error(fmt::format("Gzip decompressed size exceeds expected array size ({})", decompressed.size()));
        }
        throw std::runtime_error(fmt::format("Gzip decompression failed: {}", zError(status)));
    }
    return decompressed_size;
}

size_t BloscDecompress(const std::vector<uint8_t>& compressed, std::vector<uint8_t>& decompressed) {
    static std::once_flag blosc_init_once;

    if (compressed.size() < BLOSC_MIN_HEADER_LENGTH) {
        throw std::runtime_error(
            fmt::format("Blosc buffer is smaller than the header ({} < {})", compressed.size(), BLOSC_MIN_HEADER_LENGTH));
    }

    size_t decompressed_bytes = 0;
    size_t compressed_bytes = 0;
    size_t block_size = 0;
    blosc_cbuffer_sizes(compressed.data(), &decompressed_bytes, &compressed_bytes, &block_size);
    if (compressed_bytes > compressed.size()) {
        throw std::runtime_error(fmt::format("Blosc buffer is truncated ({} > {})", compressed_bytes, compressed.size()));
    }
    if (decompressed_bytes != decompressed.size()) {
        throw std::runtime_error(
            fmt::format("Blosc decompressed size does not match expected chunk size ({} != {})", decompressed_bytes, decompressed.size()));
    }

    std::call_once(blosc_init_once, []() { blosc_init(); });
    const int decompressed_size = blosc_decompress(compressed.data(), decompressed.data(), decompressed.size());
    if (decompressed_size < 0) {
        throw std::runtime_error(fmt::format("Blosc decompression failed with code {}", decompressed_size));
    }
    if (static_cast<size_t>(decompressed_size) != decompressed.size()) {
        throw std::runtime_error(
            fmt::format("Blosc produced an unexpected decompressed size ({} != {})", decompressed_size, decompressed.size()));
    }
    return static_cast<size_t>(decompressed_size);
}

size_t DecompressBytesCodec(const std::vector<uint8_t>& compressed, BytesCodec codec, std::vector<uint8_t>& decompressed) {
    switch (codec) {
        case BytesCodec::Blosc:
            return BloscDecompress(compressed, decompressed);
        case BytesCodec::Gzip:
            return GzipDecompress(compressed, decompressed);
        case BytesCodec::Zstd:
            return ZstdDecompress(compressed, decompressed);
    }
    throw std::runtime_error("Unsupported bytes codec");
}

// CRC32C (Castagnoli polynomial, RFC 3720), as required by the Zarr v3 crc32c codec.
uint32_t Crc32c(const uint8_t* data, size_t size) {
    constexpr uint32_t reversed_polynomial = 0x82F63B78u;
    static const std::array<uint32_t, 256> table = []() {
        std::array<uint32_t, 256> entries{};
        for (uint32_t index = 0; index < entries.size(); ++index) {
            uint32_t crc = index;
            for (int bit = 0; bit < 8; ++bit) {
                crc = (crc & 1) ? ((crc >> 1) ^ reversed_polynomial) : (crc >> 1);
            }
            entries[index] = crc;
        }
        return entries;
    }();

    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

// Verify and remove one 4-byte CRC32C suffix (stored little-endian regardless of the bytes codec
// endianness); returns the payload size without the checksum.
size_t StripCrc32c(const uint8_t* data, size_t size, const std::string& array_name) {
    if (size < sizeof(uint32_t)) {
        throw std::runtime_error(fmt::format("Array {} chunk is too small to contain a CRC32C checksum ({} bytes)", array_name, size));
    }
    const size_t payload_size = size - sizeof(uint32_t);
    const uint32_t stored = ReadUint32(data + payload_size, true);
    const uint32_t computed = Crc32c(data, payload_size);
    if (stored != computed) {
        throw std::runtime_error(
            fmt::format("Array {} CRC32C checksum mismatch (stored {:#010x}, computed {:#010x})", array_name, stored, computed));
    }
    return payload_size;
}

// Codec layout for a Zarr v3 string array: the optional bytes-to-bytes compressor, byte order, and
// the number of crc32c codecs on each side of the compressor (in codec-chain / encode order).
struct StringCodecInfo {
    std::optional<BytesCodec> bytes_codec;
    bool little_endian = true;
    size_t crc_before_compressor = 0; // crc32c encoded before the compressor: suffix ends up inside the compressed payload
    size_t crc_after_compressor = 0;  // crc32c encoded after the compressor: suffix wraps the stored chunk on disk
};

void SetBytesCodec(StringCodecInfo& info, BytesCodec codec, const std::string& array_name) {
    if (info.bytes_codec) {
        throw std::runtime_error(
            fmt::format("Array {} uses multiple bytes compressors; only one of zstd, gzip, or blosc is supported", array_name));
    }
    info.bytes_codec = codec;
}

StringCodecInfo ParseStringCodecs(const nlohmann::json& metadata, const std::string& array_name) {
    StringCodecInfo info;
    const auto* codecs = FindJsonPtr(metadata, "/codecs");
    if (!codecs || !codecs->is_array()) {
        return info;
    }
    bool seen_bytes = false;
    for (const auto& codec : *codecs) {
        const std::string codec_name = codec.value("name", "");
        if (codec_name == "blosc") {
            SetBytesCodec(info, BytesCodec::Blosc, array_name);
        } else if (codec_name == "gzip") {
            SetBytesCodec(info, BytesCodec::Gzip, array_name);
        } else if (codec_name == "zstd") {
            SetBytesCodec(info, BytesCodec::Zstd, array_name);
        } else if (codec_name == "bytes") {
            if (seen_bytes) {
                throw std::runtime_error(fmt::format("Array {} has multiple bytes codecs in its codec chain", array_name));
            }
            seen_bytes = true;
            info.little_endian = (FindJsonValue<std::string>(codec, "/configuration/endian").value_or("little") != "big");
        } else if (codec_name == "crc32c") {
            // The compressor (if any) is recorded once seen, so its presence distinguishes the two sides.
            ++(info.bytes_codec ? info.crc_after_compressor : info.crc_before_compressor);
        } else if (codec_name == "transpose") {
            // For a 1-D array the only valid permutation is the identity [0], which is a no-op.
            const auto* order = FindJsonPtr(codec, "/configuration/order");
            if (!order || *order != nlohmann::json::array({0})) {
                throw std::runtime_error(
                    fmt::format("Array {} uses a non-identity transpose; unsupported for 1-D string decode", array_name));
            }
        } else {
            throw std::runtime_error(
                fmt::format("Array {} uses unsupported codec '{}'; only bytes, transpose, zstd, gzip, blosc, and crc32c are supported",
                    array_name, codec_name));
        }
    }
    return info;
}

struct StringArrayLayout {
    size_t num_elements;
    size_t chunk_elements;
    size_t length_bytes;
};

StringArrayLayout ParseStringArrayLayout(const nlohmann::json& metadata, const std::string& array_name) {
    auto dtype_name = FindJsonValue<std::string>(metadata, "/data_type/name");
    if (!dtype_name || *dtype_name != "fixed_length_utf32") {
        const auto* dtype_name_json = FindJsonPtr(metadata, "/data_type/name");
        const std::string actual = dtype_name_json ? dtype_name_json->dump() : "null";
        throw std::runtime_error(fmt::format("Array {} is not a fixed_length_utf32 string array (data_type name: {})", array_name, actual));
    }
    auto length_bytes_value = FindJsonValue<int64_t>(metadata, "/data_type/configuration/length_bytes");
    if (!length_bytes_value) {
        throw std::runtime_error(fmt::format("Array {} missing length_bytes", array_name));
    }
    if (*length_bytes_value <= 0 || (*length_bytes_value % static_cast<int64_t>(sizeof(uint32_t))) != 0) {
        throw std::runtime_error(fmt::format("Array {} has invalid length_bytes {}", array_name, *length_bytes_value));
    }

    const auto* shape_json = FindJsonPtr(metadata, "/shape");
    const auto* chunk_shape_json = FindJsonPtr(metadata, "/chunk_grid/configuration/chunk_shape");
    if (!shape_json || !shape_json->is_array() || !chunk_shape_json || !chunk_shape_json->is_array()) {
        throw std::runtime_error(fmt::format("Array {} missing shape or chunk_shape", array_name));
    }
    if (shape_json->size() != 1 || chunk_shape_json->size() != 1) {
        throw std::runtime_error(fmt::format("Array {} is not 1-D; only 1-D string arrays are supported", array_name));
    }
    if (!(*shape_json)[0].is_number_unsigned() && !(*shape_json)[0].is_number_integer()) {
        throw std::runtime_error(fmt::format("Array {} has an invalid shape", array_name));
    }
    if (!(*chunk_shape_json)[0].is_number_unsigned() && !(*chunk_shape_json)[0].is_number_integer()) {
        throw std::runtime_error(fmt::format("Array {} has an invalid chunk_shape", array_name));
    }

    const int64_t num_elements_value = (*shape_json)[0].get<int64_t>();
    const int64_t chunk_elements_value = (*chunk_shape_json)[0].get<int64_t>();
    if (num_elements_value < 0 || chunk_elements_value <= 0) {
        throw std::runtime_error(fmt::format("Array {} has an invalid shape or chunk_shape", array_name));
    }
    if (num_elements_value > chunk_elements_value) {
        throw std::runtime_error(fmt::format("Array {} is multi-chunk; unsupported for string decode", array_name));
    }

    return {static_cast<size_t>(num_elements_value), static_cast<size_t>(chunk_elements_value), static_cast<size_t>(*length_bytes_value)};
}

std::filesystem::path GetStringChunkPath(
    const std::filesystem::path& array_dir, const nlohmann::json& metadata, const std::string& array_name) {
    const std::string key_encoding = FindJsonValue<std::string>(metadata, "/chunk_key_encoding/name").value_or("default");
    if (key_encoding == "default") {
        const std::string separator = FindJsonValue<std::string>(metadata, "/chunk_key_encoding/configuration/separator").value_or("/");
        if (separator != "/" && separator != ".") {
            throw std::runtime_error(fmt::format("Array {} has invalid chunk key separator '{}'", array_name, separator));
        }
        return array_dir / ("c" + separator + "0");
    }
    if (key_encoding == "v2") {
        return array_dir / "0";
    }
    throw std::runtime_error(fmt::format("Array {} uses unsupported chunk key encoding '{}'", array_name, key_encoding));
}

std::vector<uint8_t> ReadChunkFile(const std::filesystem::path& chunk_path) {
    std::ifstream chunk_file(chunk_path, std::ios::binary);
    if (!chunk_file) {
        throw std::runtime_error("Failed to open array data " + chunk_path.string());
    }
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(chunk_file)), std::istreambuf_iterator<char>());
}

std::vector<uint8_t> DecodeStringChunk(
    std::vector<uint8_t> bytes, const StringCodecInfo& codec_info, const StringArrayLayout& layout, const std::string& array_name) {
    if (layout.chunk_elements > std::numeric_limits<size_t>::max() / layout.length_bytes) {
        throw std::runtime_error(fmt::format("Array {} expected byte count overflows size_t", array_name));
    }
    const size_t expected_chunk_bytes = layout.chunk_elements * layout.length_bytes;

    size_t bytes_size = bytes.size();
    // crc32c after the compressor wraps the stored chunk; verify and strip it before decompressing.
    for (size_t i = 0; i < codec_info.crc_after_compressor; ++i) {
        bytes_size = StripCrc32c(bytes.data(), bytes_size, array_name);
    }

    if (codec_info.bytes_codec) {
        bytes.resize(bytes_size); // the decompressor input must be exactly the compressed stream (zstd rejects trailing bytes)
        // crc32c before the compressor leaves its suffix inside the decompressed payload.
        if (codec_info.crc_before_compressor > (std::numeric_limits<size_t>::max() - expected_chunk_bytes) / sizeof(uint32_t)) {
            throw std::runtime_error(fmt::format("Array {} expected checksum byte count overflows size_t", array_name));
        }
        std::vector<uint8_t> decompressed(expected_chunk_bytes + (sizeof(uint32_t) * codec_info.crc_before_compressor));
        bytes_size = DecompressBytesCodec(bytes, *codec_info.bytes_codec, decompressed);
        bytes = std::move(decompressed);
    }

    // crc32c before the compressor wraps the (now decompressed) payload; strip it last.
    for (size_t i = 0; i < codec_info.crc_before_compressor; ++i) {
        bytes_size = StripCrc32c(bytes.data(), bytes_size, array_name);
    }

    if (bytes_size < expected_chunk_bytes) {
        throw std::runtime_error(fmt::format("Array {} smaller than expected ({} < {})", array_name, bytes_size, expected_chunk_bytes));
    }
    bytes.resize(bytes_size);
    return bytes;
}

} // namespace

std::vector<std::string> ReadFixedLengthUtf32StringArray(const std::filesystem::path& array_dir, const nlohmann::json& metadata) {
    const std::string array_name = array_dir.filename().string();
    const StringArrayLayout layout = ParseStringArrayLayout(metadata, array_name);
    const std::filesystem::path chunk_path = GetStringChunkPath(array_dir, metadata, array_name);

    // Missing chunk: all elements take the (empty) fill value.
    if (!std::filesystem::exists(chunk_path)) {
        return std::vector<std::string>(layout.num_elements, std::string());
    }

    std::vector<uint8_t> bytes = ReadChunkFile(chunk_path);
    const StringCodecInfo codec_info = ParseStringCodecs(metadata, array_name);
    bytes = DecodeStringChunk(std::move(bytes), codec_info, layout, array_name);
    return DecodeFixedLengthUtf32(bytes, layout.num_elements, layout.length_bytes, codec_info.little_endian);
}

} // namespace carta
