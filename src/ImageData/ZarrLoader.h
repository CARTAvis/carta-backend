/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_ZARRLOADER_H_
#define CARTA_SRC_IMAGEDATA_ZARRLOADER_H_

#include "FileLoader.h"

#include <chrono>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

namespace carta {

class ZarrLoader : public FileLoader {
public:
    explicit ZarrLoader(const std::string& filename);

    bool GetCursorSpectralData(
        std::vector<float>& data, int stokes, int cursor_x, int count_x, int cursor_y, int count_y, std::mutex& image_mutex,
        const std::function<bool()>& cancellation_requested = {},
        const std::function<bool(float progress)>& partial_callback = {}) override;

    bool GetMultiRegionSpectralData(const std::vector<RegionMaskSpec>& regions, const AxisRange& z_range, int stokes,
        const std::function<bool(const RegionSpectralBlock&)>& sink) override;

    bool UseRegionSpectralData(const casacore::IPosition& region_shape, std::mutex& image_mutex) override;
    bool GetRegionSpectralData(int region_id, const AxisRange& z_range, int stokes,
        const casacore::ArrayLattice<casacore::Bool>& mask, const casacore::IPosition& origin, std::mutex& image_mutex,
        std::map<CARTA::StatsType, std::vector<double>>& results, float& progress,
        const std::function<bool(const std::map<CARTA::StatsType, std::vector<double>>&, float)>& partial_callback = {}) override;

    // The read budget one spectral reduction may hold, in bytes; zero leaves the library its own.
    // Exists so that a test can make a reduction take more than one read, which is what a region
    // covering a large image does and what no fixture small enough to keep in a repository can.
    void SetSpectralReadBudgetBytes(std::size_t bytes) {
        _spectral_read_budget_bytes = bytes;
    }

private:
    // What one region's profile has accumulated so far.
    //
    // GetRegionSpectralData is called in a loop until it reports completion, because that loop is
    // where the caller checks whether the region still exists and whether the user still wants the
    // answer. A whole-cube profile is seconds of work, so it has to be interruptible somewhere, and
    // the seam between calls is the only one the interface offers.
    struct RegionSpectralState {
        casacore::IPosition origin;
        casacore::IPosition shape;
        AxisRange z_range;
        std::size_t channels_done = 0;
        std::map<CARTA::StatsType, std::vector<double>> stats;
    };

    void AllocateImage(const std::string& hdu) override;

    std::size_t _spectral_read_budget_bytes = 0;
    std::mutex _region_spectral_mutex;
    std::map<std::pair<int, int>, RegionSpectralState> _region_spectral;
};

}  // namespace carta

#endif  // CARTA_SRC_IMAGEDATA_ZARRLOADER_H_
