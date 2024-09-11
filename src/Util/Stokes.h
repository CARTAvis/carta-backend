/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_UTIL_STOKES_H_
#define CARTA_SRC_UTIL_STOKES_H_

#include <string>

#include <carta-protobuf/enums.pb.h>

#include <casacore/measures/Measures/Stokes.h>

#include "Image.h"

namespace carta {

class Stokes {
public:
    static CARTA::PolarizationType Get(int value);
    static CARTA::PolarizationType Get(std::string name);
    static casacore::Stokes::StokesTypes ToCasacore(CARTA::PolarizationType type);
    static std::string Name(CARTA::PolarizationType type);
    static std::string Description(CARTA::PolarizationType type);
    static bool IsComputed(int value);

    // TODO move FITS mappings here too
protected:
    static std::unordered_map<CARTA::PolarizationType, casacore::Stokes::StokesTypes> _to_casacore;
    static std::unordered_map<CARTA::PolarizationType, std::string> _description;
};

// The struct StokesSource is used to tell the file loader to get the original image interface, or get the computed stokes image interface.
// The x, y, and z ranges from the StokesSource indicate the range of image data to be calculated (for the new stokes type image).
// We usually don't want to calculate the whole image data, because it spends a lot of time.
// StokesSource will bind casacore::Slicer or casacore::ImageRegion, because the coordinate of a computed stokes image is different from
// the original image coordinate.
struct StokesSource {
    int stokes;
    AxisRange z_range;
    AxisRange x_range;
    AxisRange y_range;

    StokesSource() : stokes(-1), z_range(AxisRange(ALL_Z)), x_range(AxisRange(ALL_X)), y_range(AxisRange(ALL_Y)) {}

    StokesSource(int stokes_, AxisRange z_range_) : stokes(stokes_), z_range(z_range_), x_range(ALL_X), y_range(ALL_Y) {}

    StokesSource(int stokes_, AxisRange z_range_, AxisRange x_range_, AxisRange y_range_)
        : stokes(stokes_), z_range(z_range_), x_range(x_range_), y_range(y_range_) {}

    bool IsOriginalImage() const {
        return !Stokes::IsComputed(stokes);
    }
    bool operator==(const StokesSource& rhs) const {
        if ((stokes != rhs.stokes) || (z_range != rhs.z_range) || (x_range != rhs.x_range) || (y_range != rhs.y_range)) {
            return false;
        }
        return true;
    }
    bool operator!=(const StokesSource& rhs) const {
        if ((stokes != rhs.stokes) || (z_range != rhs.z_range) || (x_range != rhs.x_range) || (y_range != rhs.y_range)) {
            return true;
        }
        return false;
    }
};

} // namespace carta

#endif // CARTA_SRC_UTIL_STOKES_H_
