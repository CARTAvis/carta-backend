//# AnimationQueue.h: uses tbb::concurrent_queue to keep channel requests in order for animation

#pragma once

#include "Session.h"
#include <carta-protobuf/set_image_channels.pb.h>
#include <mutex>
#include <utility>
#include <tbb/concurrent_queue.h>

namespace carta {

class AnimationQueue  {
public:
    AnimationQueue(Session *session);

    void addRequest(CARTA::SetImageChannels message, uint32_t requestId);
    bool executeOne();

private:
    Session *session;
    std::mutex mutex;
    using info_t = std::pair<CARTA::SetImageChannels,uint32_t>;
    using queue_t = tbb::concurrent_queue<info_t>;
    queue_t queue;
};

} // namespace carta
