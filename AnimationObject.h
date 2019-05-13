#ifndef CARTA_BACKEND__ANIMATIONOBJECT_H_
#define CARTA_BACKEND__ANIMATIONOBJECT_H_

#include <chrono>

#include <carta-protobuf/animation.pb.h>
#include <carta-protobuf/set_image_channels.pb.h>

class AnimationObject {
    friend class Session;

    int _file_id;
    CARTA::AnimationFrame _start_frame;
    CARTA::AnimationFrame _end_frame;
    CARTA::AnimationFrame _delta_frame;
    CARTA::AnimationFrame _current_frame;
    CARTA::AnimationFrame _stop_frame;
    std::chrono::microseconds _frame_interval;
    std::chrono::time_point<std::chrono::high_resolution_clock> _t_start;
    std::chrono::time_point<std::chrono::high_resolution_clock> _t_last;
    bool _looping;
    bool _reverse_at_end;
    bool _going_forward;
    bool _always_wait;
    CARTA::CompressionType _compression_type;
    float _compression_quality;
    volatile bool _stop_called;
    int _wait_duration_ms;

public:
    AnimationObject(int file_id, CARTA::AnimationFrame& start_frame, CARTA::AnimationFrame& end_frame, CARTA::AnimationFrame& delta_frame,
        int frame_interval, bool looping, bool reverse_at_end, CARTA::CompressionType compression_type, float compression_quality,
        bool always_wait)
        : _file_id(file_id),
          _start_frame(start_frame),
          _end_frame(end_frame),
          _delta_frame(delta_frame),
          _looping(looping),
          _reverse_at_end(reverse_at_end),
          _always_wait(always_wait) {
        _compression_type = compression_type;
        _compression_quality = compression_quality;
        _current_frame = start_frame;
        _frame_interval = std::chrono::microseconds(1000) * frame_interval;
        _going_forward = true;
        _wait_duration_ms = 100;
    }
};

#endif // CARTA_BACKEND__ANIMATIONOBJECT_H_
