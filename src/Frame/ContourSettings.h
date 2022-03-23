/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND__CONTOURSETTINGS_H_
#define CARTA_BACKEND__CONTOURSETTINGS_H_

#include <vector>

#include <carta-protobuf/contour.pb.h>

namespace carta {

struct ContourSettings {
    std::vector<double> levels;
    CARTA::SmoothingMode smoothing_mode;
    int smoothing_factor;
    int decimation;
    int compression_level;
    int chunk_size;
    uint32_t reference_file_id;

    // Equality operator for checking if contour settings have changed
    bool operator==(const ContourSettings& rhs) const {
        if (this->smoothing_mode != rhs.smoothing_mode || this->smoothing_factor != rhs.smoothing_factor ||
            this->decimation != rhs.decimation || this->compression_level != rhs.compression_level ||
            this->reference_file_id != rhs.reference_file_id || this->chunk_size != rhs.chunk_size) {
            return false;
        }
        if (this->levels.size() != rhs.levels.size()) {
            return false;
        }

        for (auto i = 0; i < this->levels.size(); i++) {
            if (this->levels[i] != rhs.levels[i]) {
                return false;
            }
        }

        return true;
    }

    bool operator!=(const ContourSettings& rhs) const {
        return !(*this == rhs);
    }
};

} // namespace carta

#endif // CARTA_BACKEND__CONTOURSETTINGS_H_
