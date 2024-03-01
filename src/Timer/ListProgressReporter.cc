/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2024 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ListProgressReporter.h"
#include "Util/Message.h"

using namespace carta;

ListProgressReporter::ListProgressReporter(size_t total_steps, std::function<void(CARTA::ListProgress)> progress_callback)
    : _total_steps(total_steps),
      _num_of_steps_done(0),
      _percentage(0),
      _start_time(std::chrono::high_resolution_clock::now()),
      _progress_callback(progress_callback) {}

int ListProgressReporter::UpdateProgress() {
    ++_num_of_steps_done;
    _percentage = (float)_num_of_steps_done / (float)_total_steps;
    _current_time = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double>(_current_time - _start_time).count();
}

void ListProgressReporter::ReportFileListProgress(const CARTA::FileListType& file_list_type) {
    _progress_callback(Message::ListProgress(file_list_type, _total_steps, _num_of_steps_done, _percentage));
    _start_time = std::chrono::high_resolution_clock::now();
}
