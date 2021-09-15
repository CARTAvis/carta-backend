/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND__UTIL_H_
#define CARTA_BACKEND__UTIL_H_

#include <cassert>
#include <filesystem>
#include <string>
#include <unordered_map>

#include <uWebSockets/HttpContext.h>

#include <casacore/images/Images/ImageInterface.h>
#include <casacore/images/Images/ImageOpener.h>
#include <casacore/scimath/Mathematics/GaussianBeam.h>

#include <carta-protobuf/region_stats.pb.h>
#include <carta-protobuf/spectral_profile.pb.h>

#include "Constants.h"
#include "ImageStats/BasicStatsCalculator.h"
#include "ImageStats/Histogram.h"

// ************ Utilities *************

// ************ Data Stream Helpers *************

// ************ Region Helpers *************

// ************ structs *************
//
// Usage of the AxisRange:
//

#endif // CARTA_BACKEND__UTIL_H_
