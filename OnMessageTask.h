//# OnMessageTask.h: dequeues messages and calls appropriate Session handlers

#pragma once

#include "AnimationQueue.h"
#include "FileSettings.h"
#include "Session.h"

#include <tbb/concurrent_queue.h>
#include <tbb/task.h>
#include <string>
#include <tuple>
#include <vector>


//#define __SEQUENTIAL__

using namespace std;


class OnMessageTask : public tbb::task {
 protected:
  Session *session;
  
  tbb::task* execute();  
public:
  OnMessageTask(Session *session_ ) {
    session= session_;
    //    cerr << "OMT constructer(" << this << "),session(" << session << ")\n";
  }
  ~OnMessageTask() {
    //    cerr << " ~OMT\n";
  }
  void print_session_addr() {
    //    cerr << " PRINT OM : " << this << " sess : " << session << "\n";
  }
};

class SetImageChannelsTask : public OnMessageTask {
  tbb::task* execute();
public:
  SetImageChannelsTask(Session *session_ ) : OnMessageTask( session_ ) {}
  ~SetImageChannelsTask() {}
};


class SetImageViewTask : public OnMessageTask {
  tbb::task* execute();
 public:
  SetImageViewTask(Session *session_ ) : OnMessageTask( session_ ) {}
  ~SetImageViewTask() {}
};


class SetCursorTask : public OnMessageTask {
    tbb::task* execute();
public:
    SetCursorTask(Session *session_ ) :OnMessageTask( session_ ) {}
    ~SetCursorTask() {}
};

