/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGESTATS_CUBEHISTOGRAM_H_
#define CARTA_SRC_IMAGESTATS_CUBEHISTOGRAM_H_

#include <functional>

#include "Histogram.h"

namespace carta {

// Only a declined request may fall back to the two-pass path. Failure or cancellation must not
// restart a scan after it has already read pixels and possibly reported progress.
enum class CubeHistogramStatus { finished, declined, cancelled, failed };

struct CubeHistogramProgress {
    double progress = 0.0;
    // Valid during the callback only. Re-aggregate the provisional bins only when a report is due.
    std::function<void(BasicStats<float>&, Histogram&)> snapshot;
};
using CubeHistogramCallback = std::function<bool(const CubeHistogramProgress&)>;

} // namespace carta

#endif // CARTA_SRC_IMAGESTATS_CUBEHISTOGRAM_H_
