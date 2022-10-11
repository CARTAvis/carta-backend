/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "AxesTransformer.h"

using namespace carta;

AxesTransformer::AxesTransformer(int x_size, int y_size, int z_size, const ViewAxis& view_axis)
    : _x_size(x_size), _y_size(y_size), _z_size(z_size), _view_axis(view_axis) {
    if (_view_axis == ViewAxis::x) {
        _view_axes_sizes = {{"w", _y_size}, {"h", _z_size}, {"d", _x_size}};
        _real_axes = {{"w", "y"}, {"h", "z"}, {"d", "x"}};
    } else if (_view_axis == ViewAxis::y) {
        _view_axes_sizes = {{"w", _x_size}, {"h", _z_size}, {"d", _y_size}};
        _real_axes = {{"w", "x"}, {"h", "z"}, {"d", "y"}};
    } else {
        _view_axes_sizes = {{"w", _x_size}, {"h", _y_size}, {"d", _z_size}};
        _real_axes = {{"w", "x"}, {"h", "y"}, {"d", "z"}};
    }
}

std::unordered_map<std::string, AxisRange> AxesTransformer::GetAxesRanges(int channel) {
    if (_view_axis == ViewAxis::x) {
        return {{"x", AxisRange(channel)}, {"y", AxisRange(ALL_Y)}, {"z", AxisRange(ALL_Z)}};
    } else if (_view_axis == ViewAxis::y) {
        return {{"x", AxisRange(ALL_X)}, {"y", AxisRange(channel)}, {"z", AxisRange(ALL_Z)}};
    } else {
        return {{"x", AxisRange(ALL_X)}, {"y", AxisRange(ALL_Y)}, {"z", AxisRange(channel)}};
    }
}
