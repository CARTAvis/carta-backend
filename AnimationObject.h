#ifndef CARTA_BACKEND__ANIMATIONOBJECT_H_
#define CARTA_BACKEND__ANIMATIONOBJECT_H_

#include <tbb/task.h>
#include <chrono>
#include <iostream>

#include <carta-protobuf/animation.pb.h>
#include <carta-protobuf/set_image_channels.pb.h>

namespace CARTA {
const int AnimationFlowWindowConstant = 5;
const int AnimationFlowWindowScaler = 2;
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
    int _frame_rate;
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
        CARTA::AnimationFrame& delta_frame, int frame_rate, bool looping, bool reverse_at_end, bool always_wait)
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
    }
    int CurrentFlowWindowSize() {
        return (CARTA::AnimationFlowWindowConstant * CARTA::AnimationFlowWindowScaler * _frame_rate);
    }
    void CancelExecution() {
        _tbb_context.cancel_group_execution();
    }
    void ResetContext() {
        _tbb_context.reset();
    }
};

#endif // CARTA_BACKEND__ANIMATIONOBJECT_H_
