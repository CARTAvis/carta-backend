/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Util.h"

#include <fitsio.h>
#include <climits>
#include <cmath>
#include <fstream>
#include <regex>

#include <casacore/casa/OS/File.h>

#include "ImageData/CartaMiriadImage.h"
#include "Logger/Logger.h"

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#ifdef _BOOST_FILESYSTEM_
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

using namespace std;








