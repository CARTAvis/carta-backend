/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_LOGGER_LOGGER_H_
#define CARTA_BACKEND_LOGGER_LOGGER_H_

#include <fmt/format.h>

#define SPDLOG_FMT_EXTERNAL
#define FMT_HEADER_ONLY

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "Constants.h"

void InitLogger(bool no_log_file, int verbosity);

#endif // CARTA_BACKEND_LOGGER_LOGGER_H_
