/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_ZARRLOADER_H_
#define CARTA_SRC_IMAGEDATA_ZARRLOADER_H_

#include <string>

#include "FileLoader.h"
#include "PlaneReadAhead.h"

namespace carta {

class ZarrLoader : public FileLoader, public PlaneReadAhead {
public:
    explicit ZarrLoader(const std::string& filename);

    PlaneReadAhead* ReadAhead() override {
        return this;
    }
    std::optional<ZarrPlaneRead> Plane(int z, int stokes) const override;
    CubeHistogramStatus OnePassCubeHistogram(int stokes, int num_bins, BasicStats<float>& stats, Histogram& histogram,
        const std::function<bool()>& cancelled, const CubeHistogramCallback& progress) override;

protected:
    void AllocateImage(const std::string& hdu) override;
    // Zero uses the library's memory budget. Tests can lower it to exercise multi-read scans.
    std::size_t _histogram_read_budget_bytes = 0;
};

} // namespace carta

#endif // CARTA_SRC_IMAGEDATA_ZARRLOADER_H_
