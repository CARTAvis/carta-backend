#ifndef CARTA_BACKEND__ANIMATIONOBJECT_H_
#define CARTA_BACKEND__ANIMATIONOBJECT_H_

#include <chrono>

#include <carta-protobuf/animation.pb.h>
#include <carta-protobuf/set_image_channels.pb.h>

class AnimationObject {
    friend class Session;

    int _file_id;
    ::CARTA::AnimationFrame _start_frame;
    ::CARTA::AnimationFrame _end_frame;
    ::CARTA::AnimationFrame _delta_frame;
    ::CARTA::AnimationFrame _curr_frame;
    ::CARTA::AnimationFrame _stop_frame;
    std::chrono::microseconds _frame_interval;
    std::chrono::time_point<std::chrono::high_resolution_clock> _tStart;
    std::chrono::time_point<std::chrono::high_resolution_clock> _tLast;
    bool _looping;
    bool _reverse_at_end;
    bool _going_forward;
    bool _always_wait;
    uint8_t _compression_type;
    float _compression_quality;
    volatile bool _stop_called;
    int _waitms;

public:
    AnimationObject(int fid, ::CARTA::AnimationFrame& sf, ::CARTA::AnimationFrame& ef, ::CARTA::AnimationFrame& df, int fi, bool l,
        bool rae, uint8_t ct, float cq, bool aw)
        : _file_id(fid), _start_frame(sf), _end_frame(ef), _delta_frame(df), _looping(l), _reverse_at_end(rae), _always_wait(aw) {
        _compression_type = ct;
        _compression_quality = cq;
        _curr_frame = sf, _frame_interval = std::chrono::microseconds(1000) * fi;
        _going_forward = true;
        _waitms = 100;
    }
    ~AnimationObject() {}
};

#endif // CARTA_BACKEND__ANIMATIONOBJECT_H_
