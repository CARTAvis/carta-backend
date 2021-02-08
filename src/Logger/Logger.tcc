/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_LOGGER_LOGGER_TCC_
#define CARTA_BACKEND_LOGGER_LOGGER_TCC_

template <typename S, typename... Args>
void SpdLog(const std::string& log_tag, const LogType& log_type, bool flush_now, const S& format, Args&&... args) {
    std::shared_ptr<spdlog::logger> logger = spdlog::get(log_tag);
    if (logger) {
        switch (log_type) {
            case LogType::TRACE:
                logger->trace(fmt::format(format, args...));
                break;
            case LogType::DEBUG:
                logger->debug(fmt::format(format, args...));
                break;
            case LogType::INFO:
                logger->info(fmt::format(format, args...));
                break;
            case LogType::WARN:
                logger->warn(fmt::format(format, args...));
                break;
            default: {
                logger->error(fmt::format(format, args...));
                break;
            }
        }
        if (flush_now) {
            logger->flush();
        }
    }
}

template <typename S, typename... Args>
void PERF(const S& format, Args&&... args) {
    SpdLog(STDOUT_TAG, LogType::TRACE, true, format, args...);
}

template <typename S, typename... Args>
void DEBUG(const S& format, Args&&... args) {
    SpdLog(STDOUT_TAG, LogType::DEBUG, true, format, args...);
}

template <typename S, typename... Args>
void INFO(const S& format, Args&&... args) {
    SpdLog(STDOUT_TAG, LogType::INFO, true, format, args...);
}

template <typename S, typename... Args>
void WARN(const S& format, Args&&... args) {
    SpdLog(STDOUT_TAG, LogType::WARN, true, format, args...);
}

template <typename S, typename... Args>
void ERROR(const S& format, Args&&... args) {
    SpdLog(STDOUT_TAG, LogType::ERROR, true, format, args...);
}

#endif // CARTA_BACKEND_LOGGER_LOGGER_TCC_
