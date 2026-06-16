/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# ZarrStore.h: low-level Zarr v3 store access (open, metadata navigation, array reads)
#ifndef CARTA_SRC_IMAGEDATA_ZARRSTORE_H_
#define CARTA_SRC_IMAGEDATA_ZARRSTORE_H_

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "tensorstore/array.h"
#include "tensorstore/context.h"

namespace carta {

// Configure the process-wide TensorStore context (see SharedTensorStoreContext in ZarrStore.cc)
// shared by every ZarrStore. Must be called once at startup, before any ZarrStore is constructed;
// once the context has been built the values are fixed for the lifetime of the process.
//   file_io_concurrency   <= 0 -> unset, i.e. TensorStore's own shared default pool; positive used as-is
//   data_copy_concurrency <= 0 -> auto (omp_thread_count - file_io_concurrency, at least 1);
//                                 a positive value is used as-is
//   cache_pool_mb         0    -> chunk cache disabled
void ConfigureTensorStoreContext(int file_io_concurrency, int data_copy_concurrency, int cache_pool_mb, int omp_thread_count);

// Low-level accessor for a Zarr v3 store on disk: opens the store, navigates (consolidated)
// metadata, and reads arrays. Schema-agnostic except for locating the main "SKY" image array;
// XRADIO-specific interpretation lives in ZarrMetadata. Used for metadata parsing and auxiliary
// array reads.
class ZarrStore {
public:
    explicit ZarrStore(std::string root_path);

    // Parse the root zarr.json, pick up consolidated metadata if present, and locate the SKY array.
    // Returns false (and logs) on failure. Must be called before any accessor below.
    bool Open();

    const std::string& GetImageName() const {
        return _image_name;
    }
    const nlohmann::json& GetRootMetadata() const {
        return _root_json;
    }
    const nlohmann::json& GetImageMetadata() const {
        return _image_json;
    }

    // zarr.json metadata for a named array (cached). An empty name returns the root metadata.
    nlohmann::json GetArrayMetadata(const std::string& array_name) const;
    // The "attributes" object of a named array, or an empty object if absent.
    nlohmann::json GetAttributes(const std::string& array_name) const;
    // First string value of an attribute (scalar, or first element if it is an array); "" if absent.
    std::string GetAttributeString(const std::string& array_name, const std::string& attr_name) const;

    // Read a metadata/auxiliary numeric array via TensorStore, not the SKY image array.
    // Throws std::runtime_error on failure.
    tensorstore::SharedOffsetArray<double> ReadDoubleArray(const std::string& array_name) const;
    // Read a Zarr v3 "fixed_length_utf32" string array (TensorStore cannot handle string dtypes).
    std::vector<std::string> ReadStringArray(const std::string& array_name) const;

private:
    std::string _root_path;

    // Shared by every store (see SharedTensorStoreContext), so TensorStore uses one set of
    // concurrency and cache-pool resources rather than one per opened store.
    tensorstore::Context _context;

    std::string _image_name;
    nlohmann::json _root_json;
    nlohmann::json _image_json;
    nlohmann::json _consolidated_metadata;
    bool _has_consolidated_metadata = false;
};

} // namespace carta

#endif // CARTA_SRC_IMAGEDATA_ZARRSTORE_H_
