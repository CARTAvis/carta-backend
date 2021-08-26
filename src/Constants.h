/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# Constants.h: definitions used in the Interface Control Document
//* Others added for implementation
#ifndef CARTA_BACKEND__CONSTANTS_H_
#define CARTA_BACKEND__CONSTANTS_H_

// version
#define VERSION_ID "3.0.0-beta.1b"

// file ids
#define ALL_FILES -1

// region ids
#define CUBE_REGION_ID -2
#define IMAGE_REGION_ID -1
#define CURSOR_REGION_ID 0
#define ALL_REGIONS -10

// x axis
#define ALL_X -2

// y axis
#define ALL_Y -2

// z axis
#define DEFAULT_Z 0
#define CURRENT_Z -1
#define ALL_Z -2
#define Z_NOT_SET -3

// stokes
#define DEFAULT_STOKES 0
#define CURRENT_STOKES -1

// raster image data
#define MAX_SUBSETS 8
#define TILE_SIZE 256
#define CHUNK_SIZE 512
#define MAX_TILE_CACHE_CAPACITY 4096

// histograms
#define AUTO_BIN_SIZE -1
#define HISTOGRAM_START 0.0
#define HISTOGRAM_COMPLETE 1.0
#define HISTOGRAM_CANCEL -1.0
#define UPDATE_HISTOGRAM_PROGRESS_PER_SECONDS 2.0

// z profile calculation
#define INIT_DELTA_Z 10
#define TARGET_DELTA_TIME 50 // milliseconds
#define TARGET_PARTIAL_CURSOR_TIME 500
#define TARGET_PARTIAL_REGION_TIME 1000
#define PROFILE_COMPLETE 1.0

// file list
#define FILE_LIST_FIRST_PROGRESS_AFTER_SECS 5
#define FILE_LIST_PROGRESS_INTERVAL_SECS 2

// uWebSockets setting
#define MAX_BACKPRESSURE 256 * 1024 * 1024

#endif // CARTA_BACKEND__CONSTANTS_H_
