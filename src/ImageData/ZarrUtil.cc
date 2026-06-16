/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# ZarrUtil.cc: low-level helpers for reading Zarr v3 data that TensorStore cannot handle directly
#include "ZarrUtil.h"

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

const nlohmann::json* GetJsonPtr(const nlohmann::json& obj, const char* ptr) {
    try {
        return &obj.at(nlohmann::json::json_pointer(ptr));
    } catch (...) {
        return nullptr;
    }
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
        throw std::runtime_error(fmt::format("Blosc buffer is smaller than the header ({} < {})", compressed.size(), BLOSC_MIN_HEADER_LENGTH));
    }

    size_t decompressed_bytes = 0;
    size_t compressed_bytes = 0;
    size_t block_size = 0;
    blosc_cbuffer_sizes(compressed.data(), &decompressed_bytes, &compressed_bytes, &block_size);
    if (compressed_bytes > compressed.size()) {
        throw std::runtime_error(fmt::format("Blosc buffer is truncated ({} > {})", compressed_bytes, compressed.size()));
    }

    std::call_once(blosc_init_once, []() { blosc_init(); });
    const int decompressed_size = blosc_decompress(compressed.data(), decompressed.data(), decompressed.size());
    if (decompressed_size < 0) {
        throw std::runtime_error(fmt::format("Blosc decompression failed with code {}", decompressed_size));
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

// Codec layout for a Zarr v3 string array: the optional bytes-to-bytes compressor and byte order.
struct StringCodecInfo {
    std::optional<BytesCodec> bytes_codec;
    bool little_endian = true;
};

void SetBytesCodec(StringCodecInfo& info, BytesCodec codec, const std::string& array_name) {
    if (info.bytes_codec.has_value()) {
        throw std::runtime_error(fmt::format(
            "Array {} uses multiple bytes compressors; only one of zstd, gzip, or blosc is supported", array_name));
    }
    info.bytes_codec = codec;
}

StringCodecInfo ParseStringCodecs(const nlohmann::json& metadata, const std::string& array_name) {
    StringCodecInfo info;
    const auto* codecs = GetJsonPtr(metadata, "/codecs");
    if (!codecs || !codecs->is_array()) {
        return info;
    }
    for (const auto& codec : *codecs) {
        const std::string codec_name = codec.value("name", "");
        if (codec_name == "blosc") {
            SetBytesCodec(info, BytesCodec::Blosc, array_name);
        } else if (codec_name == "gzip") {
            SetBytesCodec(info, BytesCodec::Gzip, array_name);
        } else if (codec_name == "zstd") {
            SetBytesCodec(info, BytesCodec::Zstd, array_name);
        } else if (codec_name == "bytes") {
            if (const auto* endian = GetJsonPtr(codec, "/configuration/endian")) {
                info.little_endian = (endian->get<std::string>() != "big");
            }
        } else {
            throw std::runtime_error(fmt::format(
                "Array {} uses unsupported codec '{}'; only bytes, zstd, gzip, and blosc are supported", array_name, codec_name));
        }
    }
    return info;
}

} // namespace

std::vector<std::string> ReadZarrStringArray(const std::filesystem::path& array_dir, const nlohmann::json& metadata) {
    const std::string array_name = array_dir.filename().string();

    const auto* dtype_name = GetJsonPtr(metadata, "/data_type/name");
    if (!dtype_name || !dtype_name->is_string() || dtype_name->get<std::string>() != "fixed_length_utf32") {
        const std::string actual = dtype_name ? dtype_name->dump() : "null";
        throw std::runtime_error(
            fmt::format("Array {} is not a fixed_length_utf32 string array (data_type name: {})", array_name, actual));
    }
    const auto* length_bytes_json = GetJsonPtr(metadata, "/data_type/configuration/length_bytes");
    if (!length_bytes_json || !length_bytes_json->is_number_integer()) {
        throw std::runtime_error(fmt::format("Array {} missing length_bytes", array_name));
    }
    const size_t length_bytes = length_bytes_json->get<size_t>();
    if (length_bytes == 0 || (length_bytes % sizeof(uint32_t)) != 0) {
        throw std::runtime_error(fmt::format("Array {} has invalid length_bytes {}", array_name, length_bytes));
    }

    const auto* shape_json = GetJsonPtr(metadata, "/shape");
    const auto* chunk_shape_json = GetJsonPtr(metadata, "/chunk_grid/configuration/chunk_shape");
    if (!shape_json || !shape_json->is_array() || !chunk_shape_json || !chunk_shape_json->is_array()) {
        throw std::runtime_error(fmt::format("Array {} missing shape or chunk_shape", array_name));
    }
    if (shape_json->size() != 1) {
        throw std::runtime_error(fmt::format("Array {} is not 1-D; only 1-D string arrays are supported", array_name));
    }
    if (*shape_json != *chunk_shape_json) {
        throw std::runtime_error(fmt::format("Array {} is multi-chunk; unsupported for string decode", array_name));
    }

    const size_t num_elements = (*shape_json)[0].get<size_t>();

    // Determine the single chunk's file path using the default chunk key encoding ("c" + separator + index).
    std::string separator = "/";
    if (const auto* sep = GetJsonPtr(metadata, "/chunk_key_encoding/configuration/separator")) {
        if (sep->is_string()) {
            separator = sep->get<std::string>();
        }
    }
    std::filesystem::path chunk_path = array_dir / ("c" + separator + "0");

    // Missing chunk: all elements take the (empty) fill value.
    if (!std::filesystem::exists(chunk_path)) {
        return std::vector<std::string>(num_elements, std::string());
    }

    std::ifstream chunk_file(chunk_path, std::ios::binary);
    if (!chunk_file) {
        throw std::runtime_error("Failed to open array data " + chunk_path.string());
    }
    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(chunk_file)), std::istreambuf_iterator<char>());

    // Apply the optional bytes->bytes compressor before interpreting the UTF-32 payload.
    const StringCodecInfo codec_info = ParseStringCodecs(metadata, array_name);
    const size_t expected_bytes = num_elements * length_bytes;
    std::vector<uint8_t> bytes;
    size_t actual_bytes = 0;
    if (codec_info.bytes_codec.has_value()) {
        std::vector<uint8_t> decompressed(expected_bytes);
        actual_bytes = DecompressBytesCodec(raw, *codec_info.bytes_codec, decompressed);
        bytes = std::move(decompressed);
    } else {
        actual_bytes = raw.size();
        bytes = std::move(raw);
    }
    if (actual_bytes < expected_bytes) {
        throw std::runtime_error(fmt::format("Array {} smaller than expected ({} < {})", array_name, actual_bytes, expected_bytes));
    }

    return DecodeFixedLengthUtf32(bytes, num_elements, length_bytes, codec_info.little_endian);
}

} // namespace carta
