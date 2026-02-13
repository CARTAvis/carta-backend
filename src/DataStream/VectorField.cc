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

void VectorFieldCalculator::calc_fpi_arr(const std::vector<float>& stokes_i, const std::vector<float>& stokes_q, const std::vector<float>& stokes_u, std::vector<float>& fpi) {   
   CalcFpi calc_fpi(_q_error, _u_error, _threshold);

   for(int i=0;i<stokes_i.size();i++){
      fpi[i] = calc_fpi(stokes_i[i], stokes_q[i], stokes_u[i]);
   }   
}

void VectorFieldCalculator::calc_pi_arr(const std::vector<float>& stokes_q, const std::vector<float>& stokes_u, const std::vector<float>& threshold_source, std::vector<float>& pi) {
   CalcPi calc_pi(_q_error, _u_error, _threshold);

   for(int i=0;i<stokes_q.size();i++){
      pi[i] = calc_pi(stokes_q[i], stokes_u[i], threshold_source[i], true);
   }   
}

void VectorFieldCalculator::calc_fpi_arr( const std::vector<float>& stokes_i, const std::vector<float>& stokes_q, const std::vector<float>& stokes_u, 
                                          const std::vector<float>& threshold_source, std::vector<float>& fpi) {
   CalcFpi calc_fpi(_q_error, _u_error, _threshold);

   for(int i=0;i<stokes_i.size();i++){
      fpi[i] = calc_fpi(stokes_i[i], stokes_q[i], stokes_u[i], threshold_source[i], true);
   }   
}

void VectorFieldCalculator::apply_fpi_threshold( const std::vector<float>& stokes_i, const std::vector<float>& stokes_q, const std::vector<float>& stokes_u, 
                                                const std::vector<float>& data_source, std::vector<float>& out) {
   CalcFpi calc_fpi(_q_error, _u_error, _threshold);

   for(int i=0;i<stokes_i.size();i++){
      out[i] = calc_fpi(stokes_i[i], stokes_q[i], stokes_u[i], data_source[i], false);
   }   
}

void VectorFieldCalculator::apply_pi_threshold( const std::vector<float>& stokes_q, const std::vector<float>& stokes_u, 
                                                const std::vector<float>& data_source, std::vector<float>& out) {
   CalcPi calc_pi(_q_error, _u_error, _threshold);

   for(int i=0;i<stokes_q.size();i++){
      out[i] = calc_pi(stokes_q[i], stokes_u[i], data_source[i], false);
   }   
}

void VectorFieldCalculator::calc_pa_arr(const std::vector<float>& stokes_q, const std::vector<float>& stokes_u, const std::vector<float>& threshold_source, std::vector<float>& pa) {
   CalcPa calc_pa(_threshold);

   for(int i=0;i<pa.size();i++){
      pa[i] = calc_pa(stokes_q[i], stokes_u[i], threshold_source[i]);
   }
}

void VectorFieldCalculator::calc_pa_with_fpi_threshold(const std::vector<float>& stokes_i, const std::vector<float>& stokes_q, const std::vector<float>& stokes_u, 
                                                        std::vector<float>& pa) {
   CalcFpi calc_fpi(_q_error, _u_error, _threshold);
   CalcPa calc_pa(_threshold);
   
   for(int i=0;i<pa.size();i++){
      float fpi = calc_fpi(stokes_i[i],stokes_q[i],stokes_u[i]);
      pa[i] = calc_pa(stokes_q[i],stokes_u[i],fpi);
   }   
}                                        

void VectorFieldCalculator::calc_pa_with_pi_threshold(const std::vector<float>& stokes_q, const std::vector<float>& stokes_u, std::vector<float>& pa) {
   CalcPi calc_pi(_q_error, _u_error, _threshold);
   CalcPa calc_pa(_threshold);
   
   for(int i=0;i<pa.size();i++){
      float pi = calc_pi(stokes_q[i],stokes_u[i]);
      pa[i] = calc_pa(stokes_q[i],stokes_u[i],pi);
   }   
}                                        



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

    // std::cout << "DEBUG : " << tiles.size() << " , " << num_tile_columns << " , " << num_tile_rows << std::endl;
    // printf("DEBUG Calculate flags = %d/%d/%d/%d\n",_calculate_pi,_calculate_pa,_current_stokes_as_pi,_current_stokes_as_pa);

    for (int j = 0; j < num_tile_rows; ++j) {
        for (int i = 0; i < num_tile_columns; ++i) {
            tiles[j * num_tile_columns + i].x = i;
            tiles[j * num_tile_columns + i].y = j;
            tiles[j * num_tile_columns + i].layer = tile_layer;
        }
    }

    // Initialize stokes maps for their flags (Stokes data needed) and indices (Stokes pixel axis)
    // std::unordered_map<CARTA::PolarizationType, bool> stokes_flag{{CARTA::PolarizationType::POLARIZATION_TYPE_NONE, false},
    //     {CARTA::PolarizationType::I, false}, {CARTA::PolarizationType::Q, false}, {CARTA::PolarizationType::U, false}};

    // Set stokes flags and get their indices
    bool use_threshold_I = !std::isnan(_threshold) && _threshold_option == CARTA::PolarizationType::I;

    // TODO: eliminate this stokes_flag completely - is it really OK ?
    // stokes_flag[CARTA::PolarizationType::I] = (_fractional || use_threshold_I);
    // stokes_flag[CARTA::PolarizationType::Q] = (_calculate_pi || _calculate_pa);
    // stokes_flag[CARTA::PolarizationType::U] = (_calculate_pi || _calculate_pa);

    // Get image tiles data
    // TODO/TBD : make sure this declaration can stay before the loop and shoudn't be inside the loop as originally was. Unit test case ?
    std::unordered_map<CARTA::PolarizationType, std::vector<float>> stokes_data;
    for (int i = 0; i < tiles.size(); ++i) {
        // std::cout << "DEBUG : processing tile " << i << " (" << this << ")" << std::endl;
        // sleep(1);

        if (!_is_valid) {
            // stop invalidated calculations :
            std::cout << "DEBUG : VectorFieldCalculator::CalculateVectorField - cancelling ongoing calculation" << std::endl;
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
            if (!tile_callback(stokes_data[CARTA::PolarizationType::POLARIZATION_TYPE_NONE], bounds, _smoothing_factor,
                    CARTA::PolarizationType::POLARIZATION_TYPE_NONE, width, height)) {
                return false;
            }
        }

        // Then get I, Q, U:
        // never explicitly requesting Source::I for intensity (would rather be Source::CURRENT) :
        bool uses_I{_intensity_source == Source::FPI || _threshold_source == Source::FPI || _threshold_source == Source::I};
        if (uses_I) {
            if (!tile_callback(
                    stokes_data[CARTA::PolarizationType::I], bounds, _smoothing_factor, CARTA::PolarizationType::I, width, height)) {
                return false;
            }
        }

        bool uses_QU{_angle_source == Source::PA || _intensity_source == Source::PI || _intensity_source == Source::FPI ||
                     _threshold_source == Source::PI || _threshold_source == Source::FPI};
        if (uses_QU) {
            if (!tile_callback(
                    stokes_data[CARTA::PolarizationType::Q], bounds, _smoothing_factor, CARTA::PolarizationType::Q, width, height)) {
                return false;
            }

            if (!tile_callback(
                    stokes_data[CARTA::PolarizationType::U], bounds, _smoothing_factor, CARTA::PolarizationType::U, width, height)) {
                return false;
            }
        }

        // The body of the previous function CalculatePiPa has been moved here:
        auto response =
            Message::VectorOverlayTileData(_file_id, -1, _stokes_intensity, _stokes_angle, _compression_type, _compression_quality);
        auto* tile_pi = response.add_intensity_tiles();
        auto* tile_pa = response.add_angle_tiles();

        // Threshold cut operator to be applied
        ThresholdCut threshold_cut(_threshold);
        
        // New optimised implementation, handling separately each SOURCE case - they should be exclusive:
        std::vector<float> pa, pi;
        if (_angle_source == Source::PA) {
            pa.resize(width * height);
            CalcPa calc_pa(_threshold);
            
            if (_threshold_source == Source::FPI) {
               calc_pa_with_fpi_threshold(stokes_data[CARTA::PolarizationType::I], stokes_data[CARTA::PolarizationType::Q], stokes_data[CARTA::PolarizationType::U], pa);
            }else if (_threshold_source == Source::PI) {
               calc_pa_with_pi_threshold(stokes_data[CARTA::PolarizationType::Q], stokes_data[CARTA::PolarizationType::U], pa);
            }else if (_threshold_source == Source::I) {
              calc_pa_arr(stokes_data[CARTA::PolarizationType::Q], stokes_data[CARTA::PolarizationType::U], stokes_data[CARTA::PolarizationType::I], pa);
            }else if (_threshold_source == Source::CURRENT) {
                calc_pa_arr(stokes_data[CARTA::PolarizationType::Q], stokes_data[CARTA::PolarizationType::U], stokes_data[CARTA::PolarizationType::POLARIZATION_TYPE_NONE], pa);
            }else{
               std::transform(stokes_data[CARTA::PolarizationType::Q].begin(), stokes_data[CARTA::PolarizationType::Q].end(),
                   stokes_data[CARTA::PolarizationType::U].begin(), pa.begin(), calc_pa);
           }
        }
        
        if (_intensity_source == Source::PI) {
           pi.resize(width * height);
           CalcPi calc_pi(_q_error, _u_error, _threshold);
           
           if (_threshold_source == Source::PI) {
              std::transform(stokes_data[CARTA::PolarizationType::Q].begin(), stokes_data[CARTA::PolarizationType::Q].end(), stokes_data[CARTA::PolarizationType::U].begin(), pi.begin(), calc_pi);
           }else if (_threshold_source == Source::I) {
              calc_pi_arr(stokes_data[CARTA::PolarizationType::Q],stokes_data[CARTA::PolarizationType::U], stokes_data[CARTA::PolarizationType::I], pi);
           }else if (_threshold_source == Source::CURRENT) {
              calc_pi_arr(stokes_data[CARTA::PolarizationType::Q],stokes_data[CARTA::PolarizationType::U], stokes_data[CARTA::PolarizationType::POLARIZATION_TYPE_NONE], pi);
           }else{
              // no threshold source (or NONE)
              std::transform(stokes_data[CARTA::PolarizationType::Q].begin(), stokes_data[CARTA::PolarizationType::Q].end(), stokes_data[CARTA::PolarizationType::U].begin(), pi.begin(), calc_pi);
           }
        }   
        
        if (_intensity_source == Source::FPI) {     
           pi.resize(width * height);
           
           if (_threshold_source == Source::FPI) {
              calc_fpi_arr(stokes_data[CARTA::PolarizationType::I], stokes_data[CARTA::PolarizationType::Q], stokes_data[CARTA::PolarizationType::U], pi);
           }else if (_threshold_source == Source::I) {
              calc_fpi_arr(stokes_data[CARTA::PolarizationType::I], stokes_data[CARTA::PolarizationType::Q], stokes_data[CARTA::PolarizationType::U], stokes_data[CARTA::PolarizationType::I], pi);
           }else if (_threshold_source == Source::CURRENT) {
              // applying threshold from CURRENT data:
              calc_fpi_arr(stokes_data[CARTA::PolarizationType::I], stokes_data[CARTA::PolarizationType::Q], stokes_data[CARTA::PolarizationType::U], stokes_data[CARTA::PolarizationType::POLARIZATION_TYPE_NONE], pi);
           }else{
              // threshold = NONE (i.e. no threshold applied):
              calc_fpi_arr(stokes_data[CARTA::PolarizationType::I], stokes_data[CARTA::PolarizationType::Q], stokes_data[CARTA::PolarizationType::U], pi);
           }
        }
        
        if (SourceCurrent()) {
            if (_threshold_source == Source::FPI) {
               apply_fpi_threshold(stokes_data[CARTA::PolarizationType::I], stokes_data[CARTA::PolarizationType::Q], stokes_data[CARTA::PolarizationType::U], stokes_data[CARTA::PolarizationType::POLARIZATION_TYPE_NONE], stokes_data[CARTA::PolarizationType::POLARIZATION_TYPE_NONE]);
            }
            if (_threshold_source == Source::PI) {
               apply_pi_threshold(stokes_data[CARTA::PolarizationType::Q], stokes_data[CARTA::PolarizationType::U], stokes_data[CARTA::PolarizationType::POLARIZATION_TYPE_NONE], stokes_data[CARTA::PolarizationType::POLARIZATION_TYPE_NONE]);
            }
            if (_threshold_source == Source::CURRENT) {
                std::for_each(stokes_data[CARTA::PolarizationType::POLARIZATION_TYPE_NONE].begin(),
                    stokes_data[CARTA::PolarizationType::POLARIZATION_TYPE_NONE].end(), threshold_cut);
            }
            if (_threshold_source == Source::I) {
                std::transform(stokes_data[CARTA::PolarizationType::I].begin(), stokes_data[CARTA::PolarizationType::I].end(),
                    stokes_data[CARTA::PolarizationType::POLARIZATION_TYPE_NONE].begin(),
                    stokes_data[CARTA::PolarizationType::POLARIZATION_TYPE_NONE].begin(), threshold_cut);
            }

        }
        
        // FillTileData
        if (_intensity_source == Source::CURRENT) {
            FillTileData(tile_pi, tile.x, tile.y, tile.layer, _smoothing_factor, width, height,
                stokes_data[CARTA::PolarizationType::POLARIZATION_TYPE_NONE], _compression_type, _compression_quality);
        }

        if (_angle_source == Source::CURRENT) {
            FillTileData(tile_pa, tile.x, tile.y, tile.layer, _smoothing_factor, width, height,
                stokes_data[CARTA::PolarizationType::POLARIZATION_TYPE_NONE], _compression_type, _compression_quality);
        }

        if (_intensity_source == Source::PI || _intensity_source == Source::FPI) {
            FillTileData(
                tile_pi, tile.x, tile.y, tile.layer, _smoothing_factor, width, height, pi, _compression_type, _compression_quality);
        }

        if (_angle_source == Source::PA) {
            FillTileData(
                tile_pa, tile.x, tile.y, tile.layer, _smoothing_factor, width, height, pa, _compression_type, _compression_quality);
        }

        // Now whatever combination of current / pi / pa contains the required angle and intensity data should have the correct threshold
        // applied.

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
            // std::cout << "No compression ???" << std::endl;
        }
        // std::cout << "Test value FillTiledata = " << array[0] << " compression = " << compression_type << std::endl;
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
    std::cout << "DEBUG : VectorField::SetVectorOverlayParameters called" << std::endl;

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
        std::cout << "DEBUG : cleared stokes intensity and angle -> nothing to be done" << std::endl;
        return true;
    }

    // add the new calculator object to the container of ongoing calculations
    _calculators.push_back(calculator);
    lock_calculators.unlock();
    // std::cout << "DEBUG : object added " << std::endl;

    // start a new calculation of the vector field:
    bool ret = calculator->Calculate(progress_callback, dims, tile_callback);
    // std::cout << "DEBUG : calculation completed" << std::endl;

    // removing all calculators for now :
    //    lock_calculators.lock();
    // TODO : how to nicely remove from container without using pointer comparisons ???
    //    _vector_fields.remove_if([&calculator](const std::shared_ptr<VectorFieldCalculator>& ptr){ return (ptr == calculator);});
    //    std::cout << "DEBUG : removed vector field object from the container" << std::endl;
    //    lock_calculators.unlock(); // destructor will do it anyway, but just to make it explicilty shown here

    return ret;
}

} // namespace carta
