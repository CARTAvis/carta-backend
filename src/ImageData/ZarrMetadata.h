/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

// # ZarrMetadata.h: parse Zarr metadata
#ifndef CARTA_SRC_IMAGEDATA_ZARRMETADATA_H_
#define CARTA_SRC_IMAGEDATA_ZARRMETADATA_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <casacore/casa/Arrays/IPosition.h>
#include <casacore/casa/Arrays/Vector.h>
#include <casacore/casa/BasicSL/String.h>
#include <casacore/casa/Utilities/DataType.h>
#include <casacore/images/Images/ImageBeamSet.h>

namespace carta {

class ZarrMetadata {
public:
    explicit ZarrMetadata(const std::string& filename);
    ~ZarrMetadata();

    ZarrMetadata(const ZarrMetadata&) = delete;
    ZarrMetadata& operator=(const ZarrMetadata&) = delete;

    static bool ComputeImageDataSizeBytes(const std::string& filename, int64_t& size);

    bool Initialize();
    bool IsInitialized() const;

    struct AxisInfo {
        size_t index;
        int size;
    };

    const casacore::IPosition& GetShape() const;
    casacore::DataType GetDataType() const;
    const std::map<std::string, AxisInfo>& GetAxes() const;

    casacore::Vector<casacore::String> FitsHeaderStrings();
    bool GetBeams(casacore::ImageBeamSet& beam_set);

    // Storage encoding info (compressor, compression level, chunk shape, shard shape if any) for the
    // main SKY array, as ordered label/value pairs suitable for file info computed entries.
    std::vector<std::pair<std::string, std::string>> GetStorageInfo();

    // Stokes type (casacore::Stokes::StokesTypes) for each polarization plane.
    // Empty if there is no polarization axis. Supports arbitrary (non-uniform) Stokes ordering.
    casacore::Vector<casacore::Int> GetStokesTypes();

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;

    casacore::IPosition _shape;
    casacore::DataType _data_type = casacore::TpOther;
    std::string _filename;
    bool _initialized = false;
};

} // namespace carta

#endif // CARTA_SRC_IMAGEDATA_ZARRMETADATA_H_
