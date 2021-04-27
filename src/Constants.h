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
#define VERSION_ID "2.0.0-dev.21.04.27"

// thread counts
#define OMP_THREAD_COUNT -1
#define TBB_TASK_THREAD_COUNT 3

// file ids
#define ALL_FILES -1

// region ids
#define CUBE_REGION_ID -2
#define IMAGE_REGION_ID -1
#define CURSOR_REGION_ID 0
#define ALL_REGIONS -10

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

// scripting timeouts
#define SCRIPTING_TIMEOUT 10 // seconds

// image moments
#define REPORT_FIRST_PROGRESS_AFTER_MILLI_SECS 5000
#define REPORT_PROGRESS_EVERY_FACTOR 0.1
#define MOMENT_COMPLETE 1.0
#define OUTPUT_ID_MULTIPLIER 1000

// CARTA default region style
#define REGION_COLOR "#2EE6D6"
#define REGION_DASH_LENGTH 2
#define REGION_LINE_WIDTH 2

// Shared region polygon approximation
#define DEFAULT_VERTEX_COUNT 1000

// uWebSockets setting
#define MAX_BACKPRESSURE 256 * 1024 * 1024

// socket port
#define DEFAULT_SOCKET_PORT 3002
#define MAX_SOCKET_PORT_TRIALS 100

// logger
#define LOG_FILE_SIZE 1024 * 1024 * 5 // (Bytes)
#define ROTATED_LOG_FILES 5
#define STDOUT_TAG "stdout"
#define STDOUT_PATTERN "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v"
#define PERF_TAG "performance"
#define PERF_PATTERN "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v"

// User preferences
#ifndef CARTA_USER_FOLDER_PREFIX
#define CARTA_USER_FOLDER_PREFIX ".carta"
#endif

// Frontend folder
#ifndef CARTA_DEFAULT_FRONTEND_FOLDER
#define CARTA_DEFAULT_FRONTEND_FOLDER "../share/carta/frontend"
#endif

// Schema URLs
#define CARTA_PREFERENCES_SCHEMA_URL "https://cartavis.github.io/schemas/preference_schema_1.json"
#define CARTA_LAYOUT_SCHEMA_URL "https://cartavis.github.io/schemas/layout_schema_2.json"

#endif // CARTA_BACKEND__CONSTANTS_H_
