/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "VectorField.h"
#include "Util/Message.h"

namespace carta {

VectorFieldCalculator::VectorFieldCalculator(const CARTA::SetVectorOverlayParameters& message)
    : _file_id(message.file_id()),
      _smoothing_factor(message.smoothing_factor()),
      _fractional(message.fractional()),
      _threshold(message.threshold()),
      _debiasing(message.debiasing()),
      _q_error(message.debiasing() ? message.q_error() : 0),
      _u_error(message.debiasing() ? message.u_error() : 0),
      _stokes_intensity(message.stokes_intensity()),
      _stokes_angle(message.stokes_angle()),
      _compression_type(message.compression_type()),
      _compression_quality(message.compression_quality()),
      _threshold_option(message.threshold_option()),
      _is_valid(true),
      _angle_source(_stokes_angle == -1  ? Source::NONE
                    : _stokes_angle == 0 ? Source::CURRENT
                                         : Source::PA),
      _intensity_source(_stokes_intensity == -1  ? Source::NONE
                        : _stokes_intensity == 0 ? Source::CURRENT
                        : _fractional            ? Source::FPI
                                                 : Source::PI),
      _threshold_source(std::isnan(_threshold)   ? Source::NONE
                        : _threshold_option == 0 ? Source::CURRENT
                        : _threshold_option == 1 ? Source::I
                        : _fractional            ? Source::FPI
                                                 : Source::PI) {}

bool VectorFieldCalculator::Calculate(
    const std::function<void(CARTA::VectorOverlayTileData&)>& progress_callback, DimsInfo& dims, TileCallback tile_callback) {
    // TODO : Tiles initialisation - this will use some global TilePool object
    // Get tiles
    std::vector<Tile> tiles;
    int tile_size_original = TILE_SIZE * _smoothing_factor;
    int num_tile_columns = ceil((double)dims.width / tile_size_original);
    int num_tile_rows = ceil((double)dims.height / tile_size_original);
    int32_t tile_layer = -1;
    tiles.resize(num_tile_rows * num_tile_columns);

    for (int j = 0; j < num_tile_rows; ++j) {
        for (int i = 0; i < num_tile_columns; ++i) {
            tiles[j * num_tile_columns + i].x = i;
            tiles[j * num_tile_columns + i].y = j;
            tiles[j * num_tile_columns + i].layer = tile_layer;
        }
    }

    // Set stokes flags and get their indices
    bool use_threshold_I = !std::isnan(_threshold) && _threshold_option == Pol::I;

    // Get image tiles data
    // TODO/TBD : make sure this declaration can stay before the loop and shoudn't be inside the loop as originally was. Unit test case ?
    std::unordered_map<Pol, std::vector<float>> stokes_data;
    for (int i = 0; i < tiles.size(); ++i) {
        if (!_is_valid) {
            // stop invalidated calculations :
            spdlog::debug("VectorFieldCalculator::CalculateVectorField - cancelling ongoing calculation.");
            break;
        }

        Tile& tile = tiles[i];
        // removed GetImageBounds and moved its body below, dims.width/height are size_t so cast to int() was added below
        int tile_size_original = TILE_SIZE * _smoothing_factor;
        CARTA::ImageBounds bounds;
        bounds.set_x_min(std::min(std::max(0, tile.x * tile_size_original), int(dims.width)));
        bounds.set_x_max(std::min(int(dims.width), (tile.x + 1) * tile_size_original));
        bounds.set_y_min(std::min(std::max(0, tile.y * tile_size_original), int(dims.height)));
        bounds.set_y_max(std::min(int(dims.height), (tile.y + 1) * tile_size_original));

        int width, height;
        double progress = (double)(i + 1) / tiles.size();

        // First get the current data
        if (UsesCurrent()) {
            if (!tile_callback(
                    stokes_data[Pol::POLARIZATION_TYPE_NONE], bounds, _smoothing_factor, Pol::POLARIZATION_TYPE_NONE, width, height)) {
                return false;
            }
        }

        // Then get I, Q, U:
        // never explicitly requesting Source::I for intensity (would rather be Source::CURRENT) :
        if (_intensity_source == Source::FPI || _threshold_source == Source::FPI || _threshold_source == Source::I) {
            if (!tile_callback(stokes_data[Pol::I], bounds, _smoothing_factor, Pol::I, width, height)) {
                return false;
            }
        }

        if (_angle_source == Source::PA || _intensity_source == Source::PI || _intensity_source == Source::FPI ||
            _threshold_source == Source::PI || _threshold_source == Source::FPI) {
            if (!tile_callback(stokes_data[Pol::Q], bounds, _smoothing_factor, Pol::Q, width, height)) {
                return false;
            }

            if (!tile_callback(stokes_data[Pol::U], bounds, _smoothing_factor, Pol::U, width, height)) {
                return false;
            }
        }

        // The body of the previous function CalculatePiPa has been moved here:
        auto response =
            Message::VectorOverlayTileData(_file_id, -1, _stokes_intensity, _stokes_angle, _compression_type, _compression_quality);
        auto* tile_intensity = response.add_intensity_tiles();
        auto* tile_angle = response.add_angle_tiles();

        Pol pi_key{(_fractional ? Pol::PFlinear : Pol::Plinear)};
        auto calc_pa = [&](float q, float u) { return (float)(180.0 / M_PI) * std::atan2(u, q) / 2; };
        Pol current{Pol::POLARIZATION_TYPE_NONE};
        auto& Q = stokes_data[Pol::Q];
        auto& U = stokes_data[Pol::U];
        auto& I = stokes_data[Pol::I];
        auto& C = stokes_data[current];
        auto& pi = stokes_data[pi_key];
        auto& pa = stokes_data[Pol::Pangle];
        
        std::function<void(int)> calc_pi;
        const double _error_term = (std::pow(_q_error, 2) + std::pow(_u_error, 2)) / 2.0;
        if(_fractional) {
           calc_pi = [&](int i) {
              if (!std::isnan(Q[i]) && !std::isnan(U[i])) {
                 pi[i] = std::sqrt(std::pow(Q[i], 2) + std::pow(U[i], 2) - _error_term);
                 if (!std::isnan(I[i])) {
                     pi[i] = (float)(100.0 * (pi[i] / I[i]));
                 } else {
                     pi[i] = FLOAT_NAN;
                 }
              } else {
                 pi[i] = FLOAT_NAN;
              }
           };
        } else {
           calc_pi = [&](int i) {
              if (!std::isnan(Q[i]) && !std::isnan(U[i])) {
                 pi[i] = std::sqrt(std::pow(Q[i], 2) + std::pow(U[i], 2) - _error_term);
              } else {
                 pi[i] = FLOAT_NAN;
              }
           };
        }
        
        
        // not using switch because of : "In C++, references cannot be rebound (reseated) once they are initialized."
        // so auto& T = something, and then later T = something_else; will just call assignment operator (not change of reference)
        auto& T = (_threshold_source == Source::PI ? stokes_data[Pol::Plinear] : // change to switch statement
                       _threshold_source == Source::FPI ? stokes_data[Pol::PFlinear]
                   : _threshold_source == Source::I     ? stokes_data[Pol::I]
                   : _threshold_source == Source::CURRENT
                       ? stokes_data[current]
                       : stokes_data[current] // TODO: check what to put as threshold source as default (or nothing matches)
        );

        if (_angle_source == Source::PA) {
            pa.resize(width * height);
        }

        // if _threshold_source is any of these, it is != Source::NONE -> _threshold != NaN (see constructor code above)
        if (_threshold_source == Source::PI || _threshold_source == Source::FPI || _intensity_source == Source::PI ||
            _intensity_source == Source::FPI) {
            pi.resize(width * height);

            // there is a bit of code duplication below, but it makes the code easier to understand and follow and
            // also in the current way we avoid more conditionals inside the loops, which may make it a bit faster (probably negligible
            // though)
            if (_threshold_source == Source::PI || _threshold_source == Source::FPI) {
                // handle all cases when _threshold_source is PI or FPI here
                for (int i = 0; i < Q.size(); i++) {
                    // threshold applied to PI or FPI itself :
                    calc_pi(i);
                    if (pi[i] < _threshold) {
                        pi[i] = FLOAT_NAN;
                    }

                    // threshold on PI or FPI applied to other quantities so that it is all done in a single pass here:
                    if (_angle_source == Source::PA) {
                        // because _threshold_source == Source::PI or FPI we do not need to have
                        // (std::isnan(_threshold) || T[i] >= _threshold) as this is not the case (_threshold != NaN)
                        if (!std::isnan(Q[i]) && !std::isnan(U[i]) && T[i] >= _threshold) {
                            pa[i] = calc_pa(Q[i], U[i]);
                        } else {
                            pa[i] = FLOAT_NAN;
                        }
                    }
                    if (_intensity_source == Source::CURRENT || _angle_source == Source::CURRENT) {
                        // no need to check if_threshold != NaN because we are in if _threshold_source = Source::PI or FPI
                        // i.e. != NONE which implies _threshold != NaN (see constructor code above)
                        if (!std::isnan(C[i]) && (std::isnan(T[i]) || T[i] < _threshold)) {
                            C[i] = FLOAT_NAN;
                        }
                    }
                }
            } else {
                if (_intensity_source == Source::PI || _intensity_source == Source::FPI) {
                    // There is a bit of code duplication in the if/else below, but this is to
                    // remove threshold checks in the case _threshold_source == Source::NONE :
                    if (_threshold_source != Source::NONE) {
                        for (int i = 0; i < Q.size(); i++) {
                            calc_pi(i);
                            if( T[i] < _threshold) { // _threshold != NaN because _threshold_source != Source::NONE in the if above
                                pi[i] = FLOAT_NAN;
                            }
                        }
                    } else { // _threshold_source == Source::NONE -> no need to check thresholds:
                        for (int i = 0; i < Q.size(); i++) {                            
                            calc_pi(i);
                        }
                    }
                }
            }
        }

        // calculate angle and apply required threshold:
        if (_threshold_source != Source::PI && _threshold_source != Source::FPI) {
            // cases when _threshold_source = PI or FPI have been handled earlier in a single pass calculating and applying PI/FPI to the
            // data here only cases of other thresholded values or no-threshold case
            if (_angle_source == Source::PA) {
                if (_threshold_source == Source::CURRENT || _threshold_source == Source::I) {
                    // in this case we know that _threshold != NaN -> no need to check:
                    // (std::isnan(_threshold) || T[i] >= _threshold)
                    for (int i = 0; i < Q.size(); i++) {
                        if (!std::isnan(Q[i]) && !std::isnan(U[i]) && T[i] >= _threshold) {
                            pa[i] = calc_pa(Q[i], U[i]);
                        } else {
                            pa[i] = FLOAT_NAN;
                        }
                    }
                } else {
                    // case when _threshold_source == Source::NONE, i.e. no threshold check at all:
                    for (int i = 0; i < Q.size(); i++) {
                        if (!std::isnan(Q[i]) && !std::isnan(U[i])) {
                            pa[i] = calc_pa(Q[i], U[i]);
                        } else {
                            pa[i] = FLOAT_NAN;
                        }
                    }
                }
            }

            if (_intensity_source == Source::CURRENT || _angle_source == Source::CURRENT) {
                // in this if _threshold_source can be : CURRENT, I or NONE and we split these cases
                // into 3 separate if-s to have very specific looks and avoid unnecessary isnan(_threshold)
                // checks whenever possible.
                //
                //   apply threshold cut to the CURRENT data :
                if (_threshold_source == Source::CURRENT) {
                    for (int i = 0; i < C.size(); i++) {
                        // no need for !std::isnan(_threshold) here as we know that _threshold != NaN
                        // because _threshold_source == Source::CURRENT (see constructor settings)
                        if (!std::isnan(C[i]) && C[i] < _threshold) {
                            C[i] = FLOAT_NAN;
                        }
                    }
                } else if (_threshold_source == Source::I) {
                         // if _threshold_source == Source::NONE then nothing needs to be done
                    for (int i = 0; i < C.size(); i++) {
                        // no need for !std::isnan(_threshold) here as we know that _threshold != NaN
                        // because _threshold_source == Source::I (see constructor settings)
                        if (!std::isnan(C[i]) && (std::isnan(T[i]) || T[i] < _threshold)) {
                            C[i] = FLOAT_NAN;
                        }
                    }
                }
            }
        }

        // FillTileData
        if (_intensity_source != Source::NONE) {
            auto& intensity_data = (_intensity_source == Source::CURRENT ? stokes_data[Pol::POLARIZATION_TYPE_NONE] : pi);
            FillTileData(tile_intensity, tile.x, tile.y, tile.layer, _smoothing_factor, width, height, intensity_data, _compression_type,
                _compression_quality);
        }

        if (_angle_source != Source::NONE) {
            auto& angle_data = (_angle_source == Source::CURRENT ? stokes_data[Pol::POLARIZATION_TYPE_NONE] : pa);
            FillTileData(tile_angle, tile.x, tile.y, tile.layer, _smoothing_factor, width, height, angle_data, _compression_type,
                _compression_quality);
        }

        // Send response message
        response.set_progress(progress);
        progress_callback(response);
    }

    return true;
}

void VectorFieldCalculator::FillTileData(CARTA::TileData* tile, int32_t x, int32_t y, int32_t layer, int32_t mip, int32_t tile_width,
    int32_t tile_height, std::vector<float>& array, CARTA::CompressionType compression_type, float compression_quality) {
    if (tile) {
        tile->set_x(x);
        tile->set_y(y);
        tile->set_layer(layer);
        tile->set_mip(mip);
        tile->set_width(tile_width);
        tile->set_height(tile_height);
        if (compression_type == CARTA::CompressionType::ZFP) {
            // Get and fill the NaN data
            auto nan_encodings = GetNanEncodingsBlock(array, 0, tile_width, tile_height);
            tile->set_nan_encodings(nan_encodings.data(), sizeof(int32_t) * nan_encodings.size());
            // Compress and fill the data
            std::vector<char> compression_buffer;
            size_t compressed_size;
            int precision = lround(compression_quality);
            Compress(array, 0, compression_buffer, compressed_size, tile_width, tile_height, precision);
            tile->set_image_data(compression_buffer.data(), compressed_size);
        } else {
            tile->set_image_data(array.data(), sizeof(float) * array.size());
        }
    }
}

VectorField::VectorField() : _stopped(false) {}

void VectorField::StopCalculations() {
    // stop flag set first so that new calculations are not started, then wait for mutex to invalidate all the on-going calculations
    _stopped = true;

    // lock the object and the list of calculators :
    std::unique_lock lock_calculators(_mutex);

    // Invalidate all on-going calculation to stop them :
    for (auto calculator : _calculators) {
        calculator->Invalidate();
    }
}

void VectorField::SetVectorOverlayParameters(const CARTA::SetVectorOverlayParameters& parameters) {
    spdlog::debug("VectorField::SetVectorOverlayParameters called");

    // FUTURE optimisations may required this check and function Equivalent to be back to avoid re-calculation with the same parameters
    //    but for now calculation happens everytime it is called
    //    if( VectorFieldCalculator::Equivalent(message,_vector_field_request_message) ){
    // requesting the same calculation as before -> no need for this
    //        return false;
    //    }
    _parameters = parameters;
}

bool VectorField::NewCalculation(const std::function<void(CARTA::VectorOverlayTileData&)>& progress_callback, DimsInfo& dims,
    TileCallback tile_callback, bool stokes_changed /*=false*/, bool z_changed /*=false*/) {
    // making local copy of the message in case it changes as the calculation goes on:
    auto parameters = _parameters;

    // lock the mutex to invalidate all the calculators :
    std::unique_lock lock_calculators(_mutex);

    // this needs to be after this mutex so that if the mutex is locked first in StopCalculation we exit here
    // or if mutex here is locked first we start new calculation, but it gets invalidated (stoped) when StopCalculation acquires the mutex
    if (_stopped) {
        return true;
    }

    // create new calculator and add to the vector of on-going calculators :
    // this must happen after invalidation of the on-going calculations
    auto calculator = std::make_shared<VectorFieldCalculator>(parameters);

    // moved from the start of the function here where we already have calculator object created
    // TODO/DOUBT: however, now I have a doubt if this is ok now. It can happen that we invalidate all the on-going calculations, but here
    // we then decide that there is nothing to be done and we just return.
    // Perhaps these two checks before should remain at the start of the function, but we do not have calculator object created then
    // so we would have to evalulate UsesCurrent() and Disabled() from the "first principles" as in the constructor of VectorFieldCalculator
    // object. TBD ...

    // stokes changed but we are not using current stokes -> we should return now so that we do not do
    // not needed calculations:
    if (stokes_changed && !z_changed &&
        !calculator->UsesCurrent()) { // was : parameters.stokes_intensity() != 0 && parameters.stokes_angle() != 0
        // TODO : review this part as I do not fully understand it yet ...
        return true;
    }

    // Invalidate all on-going calculation to stop them (moved from VectorField::SetVectorOverlayParameters)
    for (auto calculator_it : _calculators) {
        calculator_it->Invalidate();
    }

    // remvoing all objects from the container after they all got invalidated :
    _calculators.clear();

    // TODO : review this part it was != CURRENT but I've changed it back to 0 here as we removed the enum NONE=-1, CURRENT=0, COMPUTED=1
    if (calculator->Disabled()) { // was parameters.stokes_intensity() == -1 && parameters.stokes_angle() == -1
        spdlog::debug("Cleared stokes intensity and angle -> nothing to be done.");
        return true;
    }

    // add the new calculator object to the container of ongoing calculations
    _calculators.push_back(calculator);
    lock_calculators.unlock();

    // start a new calculation of the vector field:
    bool ret = calculator->Calculate(progress_callback, dims, tile_callback);

    // removing all calculators for now :
    //    lock_calculators.lock();
    // TODO : how to nicely remove from container without using pointer comparisons ???
    //    _vector_fields.remove_if([&calculator](const std::shared_ptr<VectorFieldCalculator>& ptr){ return (ptr == calculator);});
    //    std::cout << "DEBUG : removed vector field object from the container" << std::endl;
    //    lock_calculators.unlock(); // destructor will do it anyway, but just to make it explicilty shown here

    return ret;
}

} // namespace carta
