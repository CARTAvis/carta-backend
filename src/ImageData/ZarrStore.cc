/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

// # ZarrStore.cc: low-level Zarr v3 store access (open, metadata navigation, array reads)
#include "ZarrStore.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include "ZarrUtil.h"

#include "tensorstore/context.h"
#include "tensorstore/open.h"
#include "tensorstore/open_mode.h"
#include "tensorstore/spec.h"
#include "tensorstore/static_cast.h"
#include "tensorstore/tensorstore.h"
#include "tensorstore/util/result.h"

namespace ts = tensorstore;

namespace carta {

namespace {

constexpr const char* ZARR_JSON = "zarr.json";

constexpr int64_t BYTES_PER_MB = 1024 * 1024;

std::filesystem::path ResolveArrayPath(const std::string& root_path, const std::string& array_name) {
    const std::filesystem::path relative_path(array_name);
    if (relative_path.is_absolute() || relative_path.has_root_name() || array_name.find('\\') != std::string::npos) {
        throw std::runtime_error("Invalid Zarr array path " + array_name);
    }
    for (const auto& component : relative_path) {
        if (component == "." || component == "..") {
            throw std::runtime_error("Invalid Zarr array path " + array_name);
        }
    }

    const std::filesystem::path canonical_root = std::filesystem::weakly_canonical(std::filesystem::absolute(root_path));
    const std::filesystem::path canonical_target = std::filesystem::weakly_canonical(canonical_root / relative_path);
    auto root_component = canonical_root.begin();
    auto target_component = canonical_target.begin();
    for (; root_component != canonical_root.end(); ++root_component, ++target_component) {
        if (target_component == canonical_target.end() || *target_component != *root_component) {
            throw std::runtime_error("Zarr array path escapes store root: " + array_name);
        }
    }
    return canonical_target;
}

struct ContextConfig {
    int file_io_concurrency = 0;
    int data_copy_concurrency = 0;
    int64_t cache_pool_bytes = 0;
};

ContextConfig config;

ts::Context SharedTensorStoreContext() {
    static ts::Context context = [] {
        nlohmann::json context_spec = nlohmann::json::object();
        if (config.file_io_concurrency > 0) {
            context_spec["file_io_concurrency"] = {{"limit", config.file_io_concurrency}};
        }
        if (config.data_copy_concurrency > 0) {
            context_spec["data_copy_concurrency"] = {{"limit", config.data_copy_concurrency}};
        }
        // total_bytes_limit 0 (the default) disables caching, which is a valid configuration.
        context_spec["cache_pool"] = {{"total_bytes_limit", config.cache_pool_bytes}};

        auto context_result = ts::Context::FromJson(context_spec);
        if (!context_result.ok()) {
            spdlog::warn(
                "Failed to build TensorStore context from settings ({}); falling back to defaults", context_result.status().ToString());
            return ts::Context::Default();
        }
        spdlog::debug("TensorStore context: file_io_concurrency={}, data_copy_concurrency={}, cache_pool_mb={}", config.file_io_concurrency,
            config.data_copy_concurrency, config.cache_pool_bytes / BYTES_PER_MB);
        return std::move(context_result).value();
    }();
    return context;
}

std::vector<int64_t> ParseIntArray(const nlohmann::json* arr) {
    std::vector<int64_t> values;
    if (!arr || !arr->is_array()) {
        return values;
    }
    values.reserve(arr->size());
    for (const auto& dim : *arr) {
        if (!dim.is_number_integer()) {
            return {};
        }
        values.push_back(dim.get<int64_t>());
    }
    return values;
}

std::string FormatBloscCompressor(const nlohmann::json& codec) {
    std::string compressor = "Blosc";
    if (auto cname = FindJsonValue<std::string>(codec, "/configuration/cname")) {
        compressor += " + " + *cname;
    }

    std::string params;
    const auto* clevel = FindJsonPtr(codec, "/configuration/clevel");
    if (clevel && clevel->is_number()) {
        params += "level " + std::to_string(clevel->get<int>());
    }
    if (auto shuffle = FindJsonValue<std::string>(codec, "/configuration/shuffle"); shuffle && *shuffle != "noshuffle") {
        params += (params.empty() ? "" : ", ") + *shuffle;
    }
    if (!params.empty()) {
        compressor += " (" + params + ")";
    }
    return compressor;
}

std::string FormatCompressor(const nlohmann::json* compression_codecs) {
    if (!compression_codecs || !compression_codecs->is_array()) {
        return {};
    }

    for (const auto& codec : *compression_codecs) {
        std::string name = codec.value("name", "");
        if (name == "blosc") {
            return FormatBloscCompressor(codec);
        }
        if (name == "zstd" || name == "gzip") {
            std::string compressor = name;
            const auto* level = FindJsonPtr(codec, "/configuration/level");
            if (level && level->is_number()) {
                compressor += " (level " + std::to_string(level->get<int>()) + ")";
            }
            return compressor;
        }
    }
    return {};
}

ZarrStore::StorageLayout ParseStorageLayout(const nlohmann::json& metadata) {
    ZarrStore::StorageLayout layout;

    const auto* grid_shape = FindJsonPtr(metadata, "/chunk_grid/configuration/chunk_shape");
    const auto* codecs = FindJsonPtr(metadata, "/codecs");
    const nlohmann::json* compression_codecs = codecs;

    if (codecs && codecs->is_array()) {
        for (const auto& codec : *codecs) {
            if (codec.value("name", "") == "sharding_indexed") {
                layout.sharded = true;
                layout.shard_shape = ParseIntArray(grid_shape);
                layout.chunk_shape = ParseIntArray(FindJsonPtr(codec, "/configuration/chunk_shape"));
                compression_codecs = FindJsonPtr(codec, "/configuration/codecs");
                break;
            }
        }
    }

    if (!layout.sharded) {
        layout.chunk_shape = ParseIntArray(grid_shape);
    }
    layout.compressor = FormatCompressor(compression_codecs);
    return layout;
}

nlohmann::json ParseJsonFile(const std::filesystem::path& json_path) {
    std::ifstream file(json_path);
    if (!file) {
        throw std::runtime_error("Failed to open " + json_path.string());
    }

    return nlohmann::json::parse(file);
}

int64_t ParseElementSizeBytes(const nlohmann::json& metadata, const std::string& array_name) {
    const auto* data_type = FindJsonPtr(metadata, "/data_type");
    if (!data_type) {
        throw std::runtime_error("Array " + array_name + " is missing data_type");
    }

    if (data_type->is_string()) {
        static const std::map<std::string, int64_t, std::less<>> element_sizes{
            {"bool", 1},
            {"int8", 1},
            {"uint8", 1},
            {"int16", 2},
            {"uint16", 2},
            {"float16", 2},
            {"int32", 4},
            {"uint32", 4},
            {"float32", 4},
            {"int64", 8},
            {"uint64", 8},
            {"float64", 8},
            {"complex64", 8},
            {"complex128", 16},
        };
        const std::string& data_type_name = data_type->get_ref<const std::string&>();
        auto element_size = element_sizes.find(data_type_name);
        if (element_size != element_sizes.end()) {
            return element_size->second;
        }
        throw std::runtime_error("Array " + array_name + " has unsupported data_type " + data_type_name);
    }

    if (data_type->is_object()) {
        // XRADIO coordinate labels use the fixed_length_utf32 extension data type. Other
        // fixed-length extension types can be sized the same way when they declare length_bytes.
        auto length_bytes = FindJsonValue<int64_t>(*data_type, "/configuration/length_bytes");
        if (length_bytes && *length_bytes > 0) {
            return *length_bytes;
        }
    }

    throw std::runtime_error("Array " + array_name + " has unsupported data_type " + data_type->dump());
}

int64_t ParseDimensionSize(const nlohmann::json& dimension, const std::string& array_name) {
    if (dimension.is_number_unsigned()) {
        const uint64_t value = dimension.get<uint64_t>();
        if (value <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
            return static_cast<int64_t>(value);
        }
    } else if (dimension.is_number_integer()) {
        const int64_t value = dimension.get<int64_t>();
        if (value >= 0) {
            return value;
        }
    }
    throw std::runtime_error("Array " + array_name + " has an invalid shape dimension " + dimension.dump());
}

int64_t ComputeArraySizeBytes(const nlohmann::json& metadata, const std::string& array_name) {
    const auto* shape = FindJsonPtr(metadata, "/shape");
    if (!shape || !shape->is_array()) {
        throw std::runtime_error("Array " + array_name + " is missing shape");
    }

    int64_t total_bytes = ParseElementSizeBytes(metadata, array_name);
    for (const auto& dimension : *shape) {
        const int64_t dimension_size = ParseDimensionSize(dimension, array_name);
        if (dimension_size == 0) {
            return 0;
        }
        if (total_bytes > std::numeric_limits<int64_t>::max() / dimension_size) {
            throw std::runtime_error("Array " + array_name + " byte size overflows int64_t");
        }
        total_bytes *= dimension_size;
    }
    return total_bytes;
}

using NamedMetadata = std::pair<std::string, nlohmann::json>;

void CollectArrayMetadata(
    const std::filesystem::path& group_path, const std::filesystem::path& relative_path, std::vector<NamedMetadata>& arrays) {
    for (const auto& entry : std::filesystem::directory_iterator(group_path)) {
        std::error_code error_code;
        if (entry.is_symlink(error_code) || error_code || !entry.is_directory(error_code) || error_code) {
            continue;
        }

        const std::filesystem::path metadata_path = entry.path() / ZARR_JSON;
        if (!std::filesystem::is_regular_file(metadata_path, error_code) || error_code) {
            continue;
        }

        nlohmann::json metadata = ParseJsonFile(metadata_path);
        const std::filesystem::path child_relative_path = relative_path / entry.path().filename();
        const std::string node_type = metadata.value("node_type", "");
        if (node_type == "array") {
            arrays.emplace_back(child_relative_path.generic_string(), std::move(metadata));
        } else if (node_type == "group") {
            CollectArrayMetadata(entry.path(), child_relative_path, arrays);
        }
    }
}

ts::TensorStore<> OpenTensorStore(const std::string& array_path, const ts::Context& context) {
    auto spec_result = ts::Spec::FromJson({
        {"driver", "zarr3"},
        {"kvstore", {{"driver", "file"}, {"path", array_path}}},
    });
    if (!spec_result.ok()) {
        throw std::runtime_error("Failed to create TensorStore spec: " + spec_result.status().ToString());
    }

    auto open_result = ts::Open(spec_result.value(), context, ts::OpenMode::open, ts::ReadWriteMode::read).result();
    if (!open_result.ok()) {
        throw std::runtime_error("Failed to open TensorStore: " + open_result.status().ToString());
    }

    return std::move(open_result).value();
}

} // namespace

void ConfigureTensorStoreContext(int file_io_concurrency, int data_copy_concurrency, int cache_pool_mb, int omp_thread_count) {
    config.file_io_concurrency = file_io_concurrency;
    config.data_copy_concurrency = data_copy_concurrency > 0 ? data_copy_concurrency : std::max(1, omp_thread_count - file_io_concurrency);
    config.cache_pool_bytes = cache_pool_mb > 0 ? cache_pool_mb * BYTES_PER_MB : 0;
}

ZarrStore::ZarrStore(std::string root_path) : _root_path(std::move(root_path)), _context(SharedTensorStoreContext()) {}

bool ZarrStore::Open() {
    try {
        std::filesystem::path base_path(_root_path);
        _root_json = ParseJsonFile(base_path / ZARR_JSON);

        const auto* metadata = FindJsonPtr(_root_json, "/consolidated_metadata/metadata");
        if (metadata && metadata->is_object()) {
            _consolidated_metadata = *metadata;
            _has_consolidated_metadata = true;
        }

        return true;
    } catch (const std::exception& ex) {
        spdlog::error("Failed to load Zarr metadata from {}: {}", _root_path, ex.what());
        return false;
    }
}

nlohmann::json ZarrStore::ReadArrayMetadata(const std::string& array_name) const {
    if (array_name.empty()) {
        return _root_json;
    }

    const std::filesystem::path array_path = ResolveArrayPath(_root_path, array_name);

    if (_has_consolidated_metadata && _consolidated_metadata.contains(array_name)) {
        return _consolidated_metadata.at(array_name);
    }

    // No consolidated metadata: read the per-array zarr.json from disk.
    const std::filesystem::path array_json_path = array_path / ZARR_JSON;
    if (!std::filesystem::exists(array_json_path)) {
        return nlohmann::json::object();
    }

    return ParseJsonFile(array_json_path);
}

int64_t ZarrStore::ComputeTotalArraySizeBytes() const {
    std::vector<NamedMetadata> arrays;
    if (_root_json.value("node_type", "") == "array") {
        arrays.emplace_back("/", _root_json);
    } else if (_has_consolidated_metadata) {
        for (const auto& [array_name, metadata] : _consolidated_metadata.items()) {
            if (metadata.value("node_type", "") == "array") {
                arrays.emplace_back(array_name, metadata);
            }
        }
    }

    // An absent or empty consolidated metadata object is not enough to enumerate the store. Walk
    // group nodes instead, stopping at each array so chunk directories are never visited.
    if (arrays.empty() && _root_json.value("node_type", "") != "array") {
        CollectArrayMetadata(_root_path, {}, arrays);
    }
    if (arrays.empty()) {
        throw std::runtime_error("Zarr store contains no arrays");
    }

    int64_t total_bytes = 0;
    for (const auto& [array_name, metadata] : arrays) {
        const int64_t array_size = ComputeArraySizeBytes(metadata, array_name);
        if (total_bytes > std::numeric_limits<int64_t>::max() - array_size) {
            throw std::runtime_error("Total Zarr array byte size overflows int64_t");
        }
        total_bytes += array_size;
    }
    return total_bytes;
}

ZarrStore::StorageLayout ZarrStore::GetStorageLayout(const std::string& array_name) const {
    return ParseStorageLayout(ReadArrayMetadata(array_name));
}

ts::SharedOffsetArray<double> ZarrStore::ReadDoubleArray(const std::string& array_name) const {
    const std::filesystem::path target_path = ResolveArrayPath(_root_path, array_name);
    auto tensor_store = OpenTensorStore(target_path.string(), _context);

    auto typed_store_result = ts::StaticCast<ts::TensorStore<double>>(tensor_store);
    if (!typed_store_result.ok()) {
        throw std::runtime_error(fmt::format("Array {} is not readable as double", array_name));
    }

    auto read_result = ts::Read(typed_store_result.value()).result();
    if (!read_result.ok()) {
        throw std::runtime_error(read_result.status().ToString());
    }

    return read_result.value();
}

std::vector<std::string> ZarrStore::ReadStringArray(const std::string& array_name) const {
    nlohmann::json metadata = ReadArrayMetadata(array_name);
    return ReadFixedLengthUtf32StringArray(ResolveArrayPath(_root_path, array_name), metadata);
}

} // namespace carta
