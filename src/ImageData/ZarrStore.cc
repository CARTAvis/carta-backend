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
#include <stdexcept>
#include <utility>

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

    if (_has_consolidated_metadata && _consolidated_metadata.contains(array_name)) {
        return _consolidated_metadata.at(array_name);
    }

    // No consolidated metadata: read the per-array zarr.json from disk.
    std::filesystem::path array_json_path = std::filesystem::path(_root_path) / array_name / ZARR_JSON;
    if (!std::filesystem::exists(array_json_path)) {
        return nlohmann::json::object();
    }

    return ParseJsonFile(array_json_path);
}

ZarrStore::StorageLayout ZarrStore::GetStorageLayout(const std::string& array_name) const {
    return ParseStorageLayout(ReadArrayMetadata(array_name));
}

ts::SharedOffsetArray<double> ZarrStore::ReadDoubleArray(const std::string& array_name) const {
    std::filesystem::path target_path = std::filesystem::path(_root_path) / array_name;
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
    return ReadFixedLengthUtf32StringArray(std::filesystem::path(_root_path) / array_name, metadata);
}

} // namespace carta
