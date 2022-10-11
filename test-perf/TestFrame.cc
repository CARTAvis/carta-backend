/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "TestFrame.h"

using namespace carta;

TestFrame::TestFrame(uint32_t session_id, std::shared_ptr<carta::FileLoader> loader, const std::string& hdu, int default_z)
    : carta::Frame(session_id, loader, hdu, default_z) {}

bool TestFrame::GetLoaderSwizzledData(std::vector<float>& data, int stokes, const AxisRange& x_range, const AxisRange& y_range) {
    int x_min(0), x_count(1), y_min(0), y_count(1);
    if (x_range.from == x_range.to && x_range.from >= 0) {
        x_min = x_range.from;
        x_count = 1;
    } else if (x_range.from == x_range.to && x_range.from == ALL_X) {
        x_min = 0;
        x_count = _width;
    } else {
        spdlog::error("Unknown axis range for x!");
        return false;
    }

    if (y_range.from == y_range.to && y_range.from >= 0) {
        y_min = y_range.from;
        y_count = 1;
    } else if (y_range.from == y_range.to && y_range.from == ALL_Y) {
        y_min = 0;
        y_count = _height;
    } else {
        spdlog::error("Unknown axis range for y!");
        return false;
    }

    return _loader->GetCursorSpectralData(data, stokes, x_min, x_count, y_min, y_count, _image_mutex);
}
