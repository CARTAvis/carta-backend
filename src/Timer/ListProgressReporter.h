/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_TIMER_LISTPROGRESSREPORTER_H_
#define CARTA_BACKEND_TIMER_LISTPROGRESSREPORTER_H_

#include <chrono>
#include <functional>

#include <carta-protobuf/defs.pb.h>

namespace carta {

class ListProgressReporter {
public:
    ListProgressReporter(size_t total_steps, std::function<void(CARTA::ListProgress)> progress_callback);
    ~ListProgressReporter() = default;

    int UpdateProgress(); // return the difference of current time and start time in the unit of seconds
    void ReportFileListProgress(const CARTA::FileListType& file_list_type);

private:
    size_t _total_steps;
    size_t _num_of_steps_done;
    float _percentage;
    std::chrono::high_resolution_clock::time_point _start_time;
    std::chrono::high_resolution_clock::time_point _current_time;
    std::function<void(CARTA::ListProgress)> _progress_callback;
};

} // namespace carta

#endif // CARTA_BACKEND_TIMER_LISTPROGRESSREPORTER_H_
