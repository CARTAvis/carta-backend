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
    : ImageCache(loader_helper),
      _stokes_i(-1),
      _stokes_q(-1),
      _stokes_u(-1),
      _stokes_v(-1),
      _beam_area(_loader_helper->GetBeamArea()),
      _current_computed_stokes_channel(-1) {
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

    _memory_size = ImageMemorySize(_width, _height, _depth, _num_stokes);

    // Update the availability of full image cache size
    std::unique_lock<std::mutex> ulock(FULL_IMAGE_CACHE_SIZE_AVAILABLE_MUTEX);
    FULL_IMAGE_CACHE_SIZE_AVAILABLE -= _memory_size;
    ulock.unlock();
    spdlog::info("{:.0f} MB of full image cache are available.", FULL_IMAGE_CACHE_SIZE_AVAILABLE);
}

FullImageCache::~FullImageCache() {
    // Update the availability of full image cache size
    if (_valid) {
        std::unique_lock<std::mutex> ulock(FULL_IMAGE_CACHE_SIZE_AVAILABLE_MUTEX);
        FULL_IMAGE_CACHE_SIZE_AVAILABLE += _memory_size;
        ulock.unlock();
        spdlog::info("{:.0f} MB of full image cache are available.", FULL_IMAGE_CACHE_SIZE_AVAILABLE);
    }
}

float* FullImageCache::GetChannelData(int z, int stokes) {
    if (IsComputedStokes(stokes)) {
        if (_stokes_data.count(stokes) && _current_computed_stokes_channel == z) {
            return _stokes_data[stokes].get();
        }

        // Calculate the channel image data for computed stokes
        _stokes_data[stokes] = std::make_unique<float[]>(_width * _height);
        _current_computed_stokes_channel = z;

        auto stokes_type = StokesTypes[stokes];
        size_t start_idx = _width * _height * z;

        if (stokes_type == CARTA::PolarizationType::Ptotal) {
            if (_stokes_q > -1 && _stokes_u > -1 && _stokes_v > -1) {
#pragma omp parallel for
                for (int i = 0; i < _width * _height; ++i) {
                    size_t idx = start_idx + i;
                    _stokes_data[stokes][i] =
                        CalcPtotal(_stokes_data[_stokes_q][idx], _stokes_data[_stokes_u][idx], _stokes_data[_stokes_v][idx]);
                }
            }
        } else if (stokes_type == CARTA::PolarizationType::Plinear) {
            if (_stokes_q > -1 && _stokes_u > -1) {
#pragma omp parallel for
                for (int i = 0; i < _width * _height; ++i) {
                    size_t idx = start_idx + i;
                    _stokes_data[stokes][i] = CalcPlinear(_stokes_data[_stokes_q][idx], _stokes_data[_stokes_u][idx]);
                }
            }
        } else if (stokes_type == CARTA::PolarizationType::PFtotal) {
            if (_stokes_i > -1 && _stokes_q > -1 && _stokes_u > -1 && _stokes_v > -1) {
#pragma omp parallel for
                for (int i = 0; i < _width * _height; ++i) {
                    size_t idx = start_idx + i;
                    _stokes_data[stokes][i] = CalcPFtotal(_stokes_data[_stokes_i][idx], _stokes_data[_stokes_q][idx],
                        _stokes_data[_stokes_u][idx], _stokes_data[_stokes_v][idx]);
                }
            }
        } else if (stokes_type == CARTA::PolarizationType::PFlinear) {
            if (_stokes_i > -1 && _stokes_q > -1 && _stokes_u > -1) {
#pragma omp parallel for
                for (int i = 0; i < _width * _height; ++i) {
                    size_t idx = start_idx + i;
                    _stokes_data[stokes][i] =
                        CalcPFlinear(_stokes_data[_stokes_i][idx], _stokes_data[_stokes_q][idx], _stokes_data[_stokes_u][idx]);
                }
            }
        } else if (stokes_type == CARTA::PolarizationType::Pangle) {
            if (_stokes_q > -1 && _stokes_u > -1) {
#pragma omp parallel for
                for (int i = 0; i < _width * _height; ++i) {
                    size_t idx = start_idx + i;
                    _stokes_data[stokes][i] = CalcPangle(_stokes_data[_stokes_q][idx], _stokes_data[_stokes_u][idx]);
                }
            }
        }
        return _stokes_data[stokes].get();
    }
    return _stokes_data[stokes].get() + (_width * _height * z);
}

float FullImageCache::GetValue(int x, int y, int z, int stokes) const {
    size_t idx = (_width * _height * z) + (_width * y) + x;
    if (IsComputedStokes(stokes)) {
        if (_stokes_data.count(stokes) && _current_computed_stokes_channel == z) {
            return _stokes_data.at(stokes)[(_width * y) + x];
        }

        auto stokes_type = StokesTypes[stokes];
        if (stokes_type == CARTA::PolarizationType::Ptotal) {
            if (_stokes_q > -1 && _stokes_u > -1 && _stokes_v > -1) {
                return CalcPtotal(_stokes_data.at(_stokes_q)[idx], _stokes_data.at(_stokes_u)[idx], _stokes_data.at(_stokes_v)[idx]);
            }
        } else if (stokes_type == CARTA::PolarizationType::Plinear) {
            if (_stokes_q > -1 && _stokes_u > -1) {
                return CalcPlinear(_stokes_data.at(_stokes_q)[idx], _stokes_data.at(_stokes_u)[idx]);
            }
        } else if (stokes_type == CARTA::PolarizationType::PFtotal) {
            if (_stokes_i > -1 && _stokes_q > -1 && _stokes_u > -1 && _stokes_v > -1) {
                return CalcPFtotal(_stokes_data.at(_stokes_i)[idx], _stokes_data.at(_stokes_q)[idx], _stokes_data.at(_stokes_u)[idx],
                    _stokes_data.at(_stokes_v)[idx]);
            }
        } else if (stokes_type == CARTA::PolarizationType::PFlinear) {
            if (_stokes_i > -1 && _stokes_q > -1 && _stokes_u > -1) {
                return CalcPFlinear(_stokes_data.at(_stokes_i)[idx], _stokes_data.at(_stokes_q)[idx], _stokes_data.at(_stokes_u)[idx]);
            }
        } else if (stokes_type == CARTA::PolarizationType::Pangle) {
            if (_stokes_q > -1 && _stokes_u > -1) {
                return CalcPangle(_stokes_data.at(_stokes_q)[idx], _stokes_data.at(_stokes_u)[idx]);
            }
        }
        spdlog::error("Unknown computed stokes or its value is not available.");
        return FLOAT_NAN;
    }
    return _stokes_data.at(stokes)[idx];
}

bool FullImageCache::LoadCachedPointSpectralData(std::vector<float>& profile, int stokes, PointXy point) {
    if (_stokes_data.count(stokes) || IsComputedStokes(stokes)) {
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
