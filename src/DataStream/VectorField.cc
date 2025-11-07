/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "VectorField.h"
#include "Util/Message.h"

namespace carta {

VectorField::VectorField() : _calculate_pi(false), _calculate_pa(false), _current_stokes_as_pi(false), _current_stokes_as_pa(false) {
    ClearSettings();
}

bool VectorField::SetParameters(const CARTA::SetVectorOverlayParameters& message, int stokes_axis) {
    if (!IsEqual(message)) {
        RenewParameters(message, stokes_axis);
        return true;
    }
    return false;
}

bool VectorField::ClearParameters(const std::function<void(CARTA::VectorOverlayTileData&)>& callback, int z_index) {
    if (_smoothing_factor < 1) {
        return true;
    }

    if (_stokes_intensity < 0 && _stokes_angle < 0) {
        ClearSettings();
        auto empty_response =
            Message::VectorOverlayTileData(_file_id, z_index, _stokes_intensity, _stokes_angle, _compression_type, _compression_quality);
        empty_response.set_progress(1.0);
        callback(empty_response);
        return true;
    }
    return false;
}

// TODO/TBD : _dims and _z_index are protected in Frame -> for now passed as parameters is it ok ?
//            passing Frame by reference as well as there is probably no other way 
bool VectorField::CalculateVectorField( const std::function<void(CARTA::VectorOverlayTileData&)>& progress_callback , std::unordered_map<std::string, StokesIndex>& stokes_indices, DimsInfo& dims ,
                                        int z_index, int current_stokes_index, 
                                        const std::function< bool(std::vector<float>&, int&, int&, int, int, CARTA::ImageBounds&, int) >& tile_callback )
{
    // TODO : Tiles initialisation - this will use some global TilePool object
    // Get tiles
    std::vector<Tile> tiles;
    int tile_size_original = TILE_SIZE * _smoothing_factor;
    int num_tile_columns = ceil((double)dims.width / tile_size_original);
    int num_tile_rows = ceil((double)dims.height / tile_size_original);
    int32_t tile_layer = Tile::MipToLayer(_smoothing_factor, dims.width, dims.height, TILE_SIZE, TILE_SIZE);
    tiles.resize(num_tile_rows * num_tile_columns);

    for (int j = 0; j < num_tile_rows; ++j) {
        for (int i = 0; i < num_tile_columns; ++i) {
            tiles[j * num_tile_columns + i].x = i;
            tiles[j * num_tile_columns + i].y = j;
            tiles[j * num_tile_columns + i].layer = tile_layer;
        }
    }

    // Initialize stokes maps for their flags (Stokes data needed) and indices (Stokes pixel axis)
    std::unordered_map<std::string, bool> stokes_flag{{"I", false}, {"Q", false}, {"U", false}};
//    std::unordered_map<std::string, int> stokes_indices{{"I", -1}, {"Q", -1}, {"U", -1}};

    // Set stokes flags and get their indices
    bool use_threshold_I = !std::isnan(_threshold) && _threshold_option == CARTA::PolarizationType::I;
    stokes_flag["I"] = (_fractional || use_threshold_I) && stokes_indices["I"].valid;
    stokes_flag["Q"] = (_calculate_pi || _calculate_pa) && stokes_indices["Q"].valid;
    stokes_flag["U"] = (_calculate_pi || _calculate_pa) && stokes_indices["U"].valid;

    // Get image tiles data
    std::unordered_map<std::string, std::vector<float>> stokes_data;
    for (int i = 0; i < tiles.size(); ++i) {
        std::cout << "DEBUG : processing tile " << i << std::endl;
//        sleep(1);
        
        auto& tile = tiles[i];
        auto bounds = GetImageBounds(tile, dims.width, dims.height, _smoothing_factor );
        int width, height;
//        std::unordered_map<std::string, std::vector<float>> stokes_data; // TODO : move outside the loop, + UNIT TEST !
        double progress = (double)(i + 1) / tiles.size();

        // Get current stokes data
        if (_current_stokes_as_pi || _current_stokes_as_pa) {
            if (!tile_callback(stokes_data["CUR"], width, height, z_index, current_stokes_index, bounds, _smoothing_factor )) {
                return false;
            }
        }

        // Get stokes data I, Q, or U
        if (_calculate_pi || _calculate_pa) {
            for (auto one : stokes_flag) {
                std::string stokes = one.first;
                if (stokes_flag[stokes] &&
                    !tile_callback(stokes_data[stokes], width, height, z_index, stokes_indices[stokes].index, bounds, _smoothing_factor )) {
                    return false;
                }
            }
        }

        // The body of the previous function CalculatePiPa has been moved here:
        auto response =  Message::VectorOverlayTileData(_file_id, z_index, _stokes_intensity, _stokes_angle, _compression_type, _compression_quality);
        auto* tile_pi = response.add_intensity_tiles();
        auto* tile_pa = response.add_angle_tiles();

        // Threshold cut operator to be applied
        ThresholdCut threshold_cut(_threshold);

        // Current stokes data as PI or PA
        if (_current_stokes_as_pi || _current_stokes_as_pa) {
           // Apply a threshold cut
           std::for_each(stokes_data["CUR"].begin(), stokes_data["CUR"].end(), threshold_cut);

           if (_current_stokes_as_pi) {
              FillTileData(tile_pi, tile.x, tile.y, tile.layer, _smoothing_factor, width, height, stokes_data["CUR"], _compression_type, _compression_quality);
           }
           if (_current_stokes_as_pa) {
              FillTileData(tile_pa, tile.x, tile.y, tile.layer, _smoothing_factor, width, height, stokes_data["CUR"], _compression_type, _compression_quality);
           }
        }

        // Calculate PI and PA using stokes data I, Q or U
        std::vector<float> pi;
        if (_calculate_pi || (_calculate_pa && _threshold_option == CARTA::PolarizationType::Plinear)) {
           // Lambda function to calculate PI, errors are applied
           CalcPi calc_pi(_q_error, _u_error);
           pi.resize(width * height);
           std::transform(stokes_data["Q"].begin(), stokes_data["Q"].end(), stokes_data["U"].begin(), pi.begin(), calc_pi);
           if (_fractional) { // Calculate fractional PI
              CalcFpi calc_fpi;
              std::transform(stokes_data["I"].begin(), stokes_data["I"].end(), pi.begin(), pi.begin(), calc_fpi);
           }

           // Set NAN for PI/FPI if stokes I or Plinear (pi) is NAN or below the threshold
           if (stokes_flag["I"] && _threshold_option == CARTA::PolarizationType::I) {
              std::transform(stokes_data["I"].begin(), stokes_data["I"].end(), pi.begin(), pi.begin(), threshold_cut);
           } else {
             std::for_each(pi.begin(), pi.end(), threshold_cut);
           }

           if (_calculate_pi) {
               FillTileData( tile_pi, tile.x, tile.y, tile.layer, _smoothing_factor, width, height, pi, _compression_type, _compression_quality);
           }
        }

        if (_calculate_pa) {
           std::vector<float> pa;
           pa.resize(width * height);
           CalcPa calc_pa;
           std::transform(stokes_data["Q"].begin(), stokes_data["Q"].end(), stokes_data["U"].begin(), pa.begin(), calc_pa);

           // Set NAN for PA if stokes I or Plinear (pi) is NAN or below the threshold
           if (stokes_flag["I"] && _threshold_option == CARTA::PolarizationType::I) {
               std::transform(stokes_data["I"].begin(), stokes_data["I"].end(), pa.begin(), pa.begin(), threshold_cut);
           } else {
               std::transform(pi.begin(), pi.end(), pa.begin(), pa.begin(), threshold_cut);
           }
           FillTileData(tile_pa, tile.x, tile.y, tile.layer, _smoothing_factor, width, height, pa, _compression_type, _compression_quality);
       }

       // Send response message
       response.set_progress(progress);
       progress_callback(response);
    }
    
    return true;
}

void VectorField::ClearSettings() {
    _file_id = -1;
    _smoothing_factor = 0;
    _fractional = false;
    _threshold = DOUBLE_NAN;
    _debiasing = false;
    _q_error = 0;
    _u_error = 0;
    _stokes_intensity = -1;
    _stokes_angle = -1;
    _compression_type = CARTA::CompressionType::NONE;
    _compression_quality = 0;
}

bool VectorField::IsEqual(const CARTA::SetVectorOverlayParameters& message) {
    int file_id = message.file_id();
    int smoothing_factor = message.smoothing_factor();
    bool fractional = message.fractional();
    double threshold = message.threshold();
    bool debiasing = message.debiasing();
    double q_error = message.debiasing() ? message.q_error() : 0;
    double u_error = message.debiasing() ? message.u_error() : 0;
    int stokes_intensity = message.stokes_intensity();
    int stokes_angle = message.stokes_angle();
    CARTA::CompressionType compression_type = message.compression_type();
    float compression_quality = message.compression_quality();
    CARTA::PolarizationType threshold_option = message.threshold_option();

    return (file_id == _file_id && smoothing_factor == _smoothing_factor && fractional == _fractional && threshold == _threshold &&
            debiasing == _debiasing && q_error == _q_error && u_error == _u_error && stokes_intensity == _stokes_intensity &&
            stokes_angle == _stokes_angle && compression_type == _compression_type && compression_quality == _compression_quality &&
            threshold_option == _threshold_option);
}

void VectorField::RenewParameters(const CARTA::SetVectorOverlayParameters& message, int stokes_axis) {
    _file_id = message.file_id();
    _smoothing_factor = message.smoothing_factor();
    _fractional = message.fractional();
    _threshold = message.threshold();
    _debiasing = message.debiasing();
    _q_error = message.debiasing() ? message.q_error() : 0;
    _u_error = message.debiasing() ? message.u_error() : 0;
    _stokes_intensity = message.stokes_intensity();
    _stokes_angle = message.stokes_angle();
    _compression_type = message.compression_type();
    _compression_quality = message.compression_quality();
    _threshold_option = message.threshold_option();

    bool has_stokes_axis(stokes_axis > -1);
    _calculate_pi = _stokes_intensity == 1 && has_stokes_axis;
    _calculate_pa = _stokes_angle == 1 && has_stokes_axis;
    _current_stokes_as_pi = (_stokes_intensity == 0 && has_stokes_axis) || !has_stokes_axis;
    _current_stokes_as_pa = (_stokes_angle == 0 && has_stokes_axis) || !has_stokes_axis;
}

void VectorField::FillTileData(CARTA::TileData* tile, int32_t x, int32_t y, int32_t layer, int32_t mip, int32_t tile_width,
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

CARTA::ImageBounds GetImageBounds(const Tile& tile, int image_width, int image_height, int mip) {
    int tile_size_original = TILE_SIZE * mip;
    CARTA::ImageBounds bounds;
    bounds.set_x_min(std::min(std::max(0, tile.x * tile_size_original), image_width));
    bounds.set_x_max(std::min(image_width, (tile.x + 1) * tile_size_original));
    bounds.set_y_min(std::min(std::max(0, tile.y * tile_size_original), image_height));
    bounds.set_y_max(std::min(image_height, (tile.y + 1) * tile_size_original));
    return bounds;
}

} // namespace carta
