/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "FullImageCache.h"

#include "Logger/Logger.h"
#include "Timer/Timer.h"
#include "Util/Stokes.h"

namespace carta {

FullImageCache::FullImageCache(std::shared_ptr<LoaderHelper> loader_helper)
    : ImageCache(loader_helper), _stokes_i(-1), _stokes_q(-1), _stokes_u(-1), _stokes_v(-1), _beam_area(_loader_helper->GetBeamArea()) {
    Timer t;
    if (!_loader_helper->FillFullImageCache(_stokes_data)) {
        _valid = false;
        return;
    }
    auto dt = t.Elapsed();
    spdlog::performance("Load {}x{}x{}x{} image to cache in {:.3f} ms at {:.3f} MPix/s", _width, _height, _depth, _num_stokes, dt.ms(),
        (float)(_width * _height * _depth * _num_stokes) / dt.us());

    // Get stokes indices
    bool mute_err_msg(true);
    _loader_helper->GetStokesTypeIndex("I", _stokes_i, mute_err_msg);
    _loader_helper->GetStokesTypeIndex("Q", _stokes_q, mute_err_msg);
    _loader_helper->GetStokesTypeIndex("U", _stokes_u, mute_err_msg);
    _loader_helper->GetStokesTypeIndex("V", _stokes_v, mute_err_msg);

    t.Restart();
    FillComputedStokesCubes();
    dt = t.Elapsed();
    int num_computed_stokes = _stokes_data.size() - _num_stokes;
    if (num_computed_stokes > 0) {
        spdlog::performance("Calculate stokes hypercubes {}x{}x{}x{} in {:.3f} ms at {:.3f} MPix/s", _width, _height, _depth,
            num_computed_stokes, dt.ms(), (float)(_width * _height * _depth * num_computed_stokes) / dt.us());
    }

    // Update the availability of full image cache size
    std::unique_lock<std::mutex> ulock(FULL_IMAGE_CACHE_SIZE_AVAILABLE_MUTEX);
    FULL_IMAGE_CACHE_SIZE_AVAILABLE -= ImageMemorySize(_width, _height, _depth, _num_stokes);
    ulock.unlock();
    spdlog::info("{:.0f} MB of full image cache are available.", FULL_IMAGE_CACHE_SIZE_AVAILABLE);
}

FullImageCache::~FullImageCache() {
    // Update the availability of full image cache size
    if (_valid) {
        std::unique_lock<std::mutex> ulock(FULL_IMAGE_CACHE_SIZE_AVAILABLE_MUTEX);
        FULL_IMAGE_CACHE_SIZE_AVAILABLE += ImageMemorySize(_width, _height, _depth, _num_stokes);
        ulock.unlock();
        spdlog::info("{:.0f} MB of full image cache are available.", FULL_IMAGE_CACHE_SIZE_AVAILABLE);
    }
}

void FullImageCache::FillComputedStokesCubes() {
    if (_stokes_i > -1 && _stokes_q > -1 && _stokes_u > -1 && _stokes_v > -1) {
        int stokes = StokesValues[CARTA::PolarizationType::PFtotal];
        _stokes_data[stokes] = std::make_unique<float[]>(_width * _height * _depth);
#pragma omp parallel for
        for (size_t z = 0; z < _depth; ++z) {
            size_t start_idx = _width * _height * z;
#pragma omp parallel for
            for (int i = 0; i < _width * _height; ++i) {
                size_t idx = start_idx + i;
                _stokes_data[stokes][idx] = CalcPFtotal(
                    _stokes_data[_stokes_i][idx], _stokes_data[_stokes_q][idx], _stokes_data[_stokes_u][idx], _stokes_data[_stokes_v][idx]);
            }
        }
    }

    if (_stokes_q > -1 && _stokes_u > -1 && _stokes_v > -1) {
        int stokes = StokesValues[CARTA::PolarizationType::Ptotal];
        _stokes_data[stokes] = std::make_unique<float[]>(_width * _height * _depth);
#pragma omp parallel for
        for (size_t z = 0; z < _depth; ++z) {
            size_t start_idx = _width * _height * z;
#pragma omp parallel for
            for (int i = 0; i < _width * _height; ++i) {
                size_t idx = start_idx + i;
                _stokes_data[stokes][idx] =
                    CalcPtotal(_stokes_data[_stokes_q][idx], _stokes_data[_stokes_u][idx], _stokes_data[_stokes_v][idx]);
            }
        }
    }

    if (_stokes_i > -1 && _stokes_q > -1 && _stokes_u > -1) {
        int stokes = StokesValues[CARTA::PolarizationType::PFlinear];
        _stokes_data[stokes] = std::make_unique<float[]>(_width * _height * _depth);
#pragma omp parallel for
        for (size_t z = 0; z < _depth; ++z) {
            size_t start_idx = _width * _height * z;
#pragma omp parallel for
            for (int i = 0; i < _width * _height; ++i) {
                size_t idx = start_idx + i;
                _stokes_data[stokes][idx] =
                    CalcPFlinear(_stokes_data[_stokes_i][idx], _stokes_data[_stokes_q][idx], _stokes_data[_stokes_u][idx]);
            }
        }
    }

    if (_stokes_q > -1 && _stokes_u > -1) {
        int stokes = StokesValues[CARTA::PolarizationType::Plinear];
        _stokes_data[stokes] = std::make_unique<float[]>(_width * _height * _depth);
#pragma omp parallel for
        for (size_t z = 0; z < _depth; ++z) {
            size_t start_idx = _width * _height * z;
#pragma omp parallel for
            for (int i = 0; i < _width * _height; ++i) {
                size_t idx = start_idx + i;
                _stokes_data[stokes][idx] = CalcPlinear(_stokes_data[_stokes_q][idx], _stokes_data[_stokes_u][idx]);
            }
        }

        stokes = StokesValues[CARTA::PolarizationType::Pangle];
        _stokes_data[stokes] = std::make_unique<float[]>(_width * _height * _depth);
#pragma omp parallel for
        for (size_t z = 0; z < _depth; ++z) {
            size_t start_idx = _width * _height * z;
#pragma omp parallel for
            for (int i = 0; i < _width * _height; ++i) {
                size_t idx = start_idx + i;
                _stokes_data[stokes][idx] = CalcPangle(_stokes_data[_stokes_q][idx], _stokes_data[_stokes_u][idx]);
            }
        }
    }
}

float* FullImageCache::GetChannelData(int z, int stokes) {
    return _stokes_data[stokes].get() + (_width * _height * z);
}

float FullImageCache::GetValue(int x, int y, int z, int stokes) const {
    return _stokes_data.at(stokes)[(_width * _height * z) + (_width * y) + x];
}

bool FullImageCache::LoadCachedPointSpectralData(std::vector<float>& profile, int stokes, PointXy point) {
    if (_stokes_data.count(stokes)) {
        int x, y;
        point.ToIndex(x, y);
        profile.resize(_depth);
#pragma omp parallel for
        for (int z = 0; z < _depth; ++z) {
            profile[z] = GetValue(x, y, z, stokes);
        }
        return true;
    }
    return false;
}

bool FullImageCache::LoadCachedRegionSpectralData(const AxisRange& z_range, int stokes, const casacore::ArrayLattice<casacore::Bool>& mask,
    const casacore::IPosition& origin, std::map<CARTA::StatsType, std::vector<double>>& profiles) {
    // Region spectral profile for computed stokes can not be directly calculated from its pixel values. It is calculated from the
    // combination of spectral profiles for stokes I, Q, U, or V.
    if (!mask.shape().empty() && _stokes_data.count(stokes) && !IsComputedStokes(stokes)) {
        auto get_value = [&](size_t idx) { return _stokes_data.at(stokes)[idx]; };
        DoStatisticsCalculations(z_range, mask, origin, _beam_area, get_value, profiles);
        return true;
    }
    return false;
}

bool FullImageCache::CachedChannelDataAvailable(int z, int stokes) const {
    return true;
}

bool FullImageCache::UpdateChannelImageCache(int z, int stokes) {
    return true;
}

void FullImageCache::SetImageChannels(int z, int stokes) {
    _loader_helper->SetImageChannels(z, stokes);
}

} // namespace carta
