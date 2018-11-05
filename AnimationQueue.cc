#include "AnimationQueue.h"

using namespace carta;

AnimationQueue::AnimationQueue(Session *session_)
    : session(session_)
{}

void AnimationQueue::addRequest(CARTA::SetImageChannels message, uint32_t requestId) {
    queue.push({message, requestId});
}

bool AnimationQueue::executeOne() {
    std::unique_lock<std::mutex> guard(mutex);
    AnimationQueue::info_t req;
    if(!queue.try_pop(req)) {
        return false;
    }
    session->onSetImageChannels(req.first, req.second);
    return true;
}
