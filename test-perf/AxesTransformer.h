/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <unordered_map>

#include "Util/Image.h"

namespace carta {

class AxesTransformer {
public:
    enum ViewAxis { x, y, z }; // axis number: x = 0, y = 1, z = 2

    AxesTransformer(int x_size, int y_size, int z_size, const ViewAxis& view_axis);

    std::unordered_map<std::string, AxisRange> GetAxesRanges(int channel);

    std::unordered_map<std::string, size_t> GetViewAxesSizes() {
        return _view_axes_sizes;
    }

    std::unordered_map<std::string, std::string> GetRealAxes() {
        return _real_axes;
    }

private:
    ViewAxis _view_axis;
    size_t _x_size, _y_size, _z_size;
    std::unordered_map<std::string, size_t> _view_axes_sizes;
    std::unordered_map<std::string, std::string> _real_axes; // key: view axis, value: real axis
};

} // namespace carta
