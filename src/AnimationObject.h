/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND__ANIMATIONOBJECT_H_
#define CARTA_BACKEND__ANIMATIONOBJECT_H_

#include <tbb/task.h>
#include <chrono>
#include <iostream>

#include <carta-protobuf/animation.pb.h>
#include <carta-protobuf/set_image_channels.pb.h>

namespace CARTA {
const int InitialAnimationWaitsPerSecond = 3;
const int InitialWindowScale = 1;
} // namespace CARTA

class AnimationObject {
    friend class Session;

    int _file_id;
    CARTA::AnimationFrame _start_frame;
    CARTA::AnimationFrame _first_frame;
    CARTA::AnimationFrame _last_frame;
    CARTA::AnimationFrame _delta_frame;
    CARTA::AnimationFrame _current_frame;
    CARTA::AnimationFrame _next_frame;
    CARTA::AnimationFrame _last_flow_frame;
    std::unordered_map<int32_t, std::vector<float>> _matched_frames;
    int _frame_rate;
    int _waits_per_second;
    int _window_scale;
    std::chrono::microseconds _frame_interval;
    std::chrono::time_point<std::chrono::high_resolution_clock> _t_start;
    std::chrono::time_point<std::chrono::high_resolution_clock> _t_last;
    bool _looping;
    bool _reverse_at_end;
    bool _going_forward;
    bool _always_wait;
    volatile bool _stop_called;
    int _wait_duration_ms;
    volatile int _file_open;
    volatile bool _waiting_flow_event;
    tbb::task_group_context _tbb_context;

public:
    AnimationObject(int file_id, CARTA::AnimationFrame& start_frame, CARTA::AnimationFrame& first_frame, CARTA::AnimationFrame& last_frame,
        CARTA::AnimationFrame& delta_frame, const google::protobuf::Map<google::protobuf::int32, CARTA::MatchedFrameList>& matched_frames,
        int frame_rate, bool looping, bool reverse_at_end, bool always_wait)
        : _file_id(file_id),
          _start_frame(start_frame),
          _first_frame(first_frame),
          _last_frame(last_frame),
          _looping(looping),
          _reverse_at_end(reverse_at_end),
          _frame_rate(frame_rate),
          _always_wait(always_wait) {
        _current_frame = start_frame;
        _next_frame = start_frame;

        if (!matched_frames.empty()) {
            for (auto const& entry : matched_frames) {
                _matched_frames[entry.first] = {entry.second.frame_numbers().begin(), entry.second.frame_numbers().end()};
            }
            // Empty array for the active file_id, since its channel will be set automatically
            _matched_frames[file_id] = {};
        }

        // handle negative deltas
        if (delta_frame.channel() < 0 || delta_frame.stokes() < 0) {
            _delta_frame.set_channel(-delta_frame.channel());
            _delta_frame.set_stokes(-delta_frame.stokes());
            _going_forward = false;
        } else {
            _delta_frame.set_channel(delta_frame.channel());
            _delta_frame.set_stokes(delta_frame.stokes());
            _going_forward = true;
        }

        _frame_interval = std::chrono::microseconds(int64_t(1.0e6 / frame_rate));
        _wait_duration_ms = 100;
        _stop_called = false;
        _file_open = true;
        _waiting_flow_event = false;
        _last_flow_frame = start_frame;
        _waits_per_second = CARTA::InitialAnimationWaitsPerSecond;
        _window_scale = CARTA::InitialWindowScale;
    }
    int CurrentFlowWindowSize() {
        return (_frame_rate / _waits_per_second) * _window_scale;
    }
    void CancelExecution() {
        _tbb_context.cancel_group_execution();
    }
    void ResetContext() {
        _tbb_context.reset();
    }
};

#endif // CARTA_BACKEND__ANIMATIONOBJECT_H_
