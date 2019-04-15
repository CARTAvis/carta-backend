
#pragma once


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
  std::chrono::milliseconds _frame_interval;
  bool _looping;
  bool _reverse_at_end;
  bool _going_forward;
  uint8_t _compression_type;
  float _compression_quality;
  volatile bool _stop_called;
 public:
 AnimationObject(int fid,
		 ::CARTA::AnimationFrame &sf,
		 ::CARTA::AnimationFrame &ef,
		 ::CARTA::AnimationFrame &df,
		 int fi,
		 bool l,
		 bool rae,
		 uint8_t ct,
		 uint8_t cq) :
  _file_id(fid), _start_frame(sf), _curr_frame(sf),
    _end_frame(ef), _delta_frame(df),
    _looping(l), _reverse_at_end(rae),
    _compression_type(ct), _compression_quality(cq) {
    _frame_interval= std::chrono::milliseconds(1)*fi;
    _going_forward= true;
  }
  ~AnimationObject() {
  }
};
