//# OnMessageTask.h: dequeues messages and calls appropriate Session handlers

#pragma once

#include "AnimationQueue.h"
#include "Session.h"
#include <tbb/concurrent_queue.h>
#include <tbb/task.h>
#include <string>
#include <tuple>
#include <vector>

class OnMessageTask : public tbb::task {
    std::string uuid;
    Session *session;
    tbb::concurrent_queue<std::tuple<std::string,uint32_t,std::vector<char>>> *mqueue;
    carta::AnimationQueue *aqueue;

    tbb::task* execute();

public:
    OnMessageTask(std::string uuid_, Session *session_,
                  tbb::concurrent_queue<std::tuple<std::string,uint32_t,std::vector<char>>> *mq,
                  carta::AnimationQueue *aq = nullptr);
};
