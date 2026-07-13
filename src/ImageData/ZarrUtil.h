/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

// # ZarrUtil.h: low-level Zarr v3 metadata and data helpers
#ifndef CARTA_SRC_IMAGEDATA_ZARRUTIL_H_
#define CARTA_SRC_IMAGEDATA_ZARRUTIL_H_

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include <nlohmann/json.hpp>

namespace carta {

inline constexpr const char* ZARR_DEFAULT_IMAGE_ARRAY = "SKY";
inline constexpr std::array<const char*, 1> ZARR_SUPPORTED_IMAGE_ARRAYS{ZARR_DEFAULT_IMAGE_ARRAY};

// Return a pointer to the JSON value at the given JSON pointer, or nullptr if it does not exist.
const nlohmann::json* FindJsonPtr(const nlohmann::json& obj, const char* ptr);

// Return the supported image arrays present in a Zarr image directory, in ZARR_SUPPORTED_IMAGE_ARRAYS order.
std::vector<std::string> ListZarrImageArrays(const std::string& path_string);

// Return true if the path is a supported XRADIO Zarr image directory.
bool IsSupportedZarrImage(const std::string& path_string);

template <typename T>
std::optional<T> FindJsonValue(const nlohmann::json& obj, const char* ptr) {
    const auto* value = FindJsonPtr(obj, ptr);
    if (!value) {
        return std::nullopt;
    }

    if constexpr (std::is_same_v<T, std::string>) {
        if (value->is_string()) {
            return value->get<std::string>();
        }
    } else if constexpr (std::is_floating_point_v<T>) {
        if (value->is_number()) {
            return value->get<T>();
        }
    } else if constexpr (std::is_integral_v<T>) {
        if (value->is_number_integer() || value->is_number_unsigned()) {
            return value->get<T>();
        }
    }

    return std::nullopt;
}

/**
 * @brief Decode a 1-D Zarr v3 "fixed_length_utf32" string array by hand.
 *
 * TensorStore's zarr3 driver does not support string data types, so the chunk is read and decoded
 * directly. Only single-chunk 1-D arrays using the default or v2 chunk key encoding are supported,
 * with a codec chain of: an optional identity transpose, bytes, at most one of zstd/gzip/blosc, and
 * any number of crc32c codecs (checksums are verified). This covers the layouts produced by XRADIO
 * for coordinate label arrays.
 *
 * @param array_dir Directory of the array (e.g. <image>/polarization).
 * @param metadata The array's zarr.json metadata.
 * @return The decoded UTF-8 strings, one per array element.
 * @throws std::runtime_error On unsupported layout or malformed data.
 */
std::vector<std::string> ReadFixedLengthUtf32StringArray(const std::filesystem::path& array_dir, const nlohmann::json& metadata);

} // namespace carta

#endif // CARTA_SRC_IMAGEDATA_ZARRUTIL_H_
