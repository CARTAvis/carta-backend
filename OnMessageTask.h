//# OnMessageTask.h: dequeues messages and calls appropriate Session handlers

#pragma once

#include "FileSettings.h"
#include "Session.h"

#include <tbb/concurrent_queue.h>
#include <tbb/task.h>
#include <string>
#include <tuple>
#include <vector>



class OnMessageTask : public tbb::task {
 protected:
  Session *session;
  virtual tbb::task* execute() = 0;
public:
  OnMessageTask( Session *session_ ) {
    session= session_;
    session->increase_ref_count();
  }
  ~OnMessageTask() {
    if( ! session->decrease_ref_count() )
      delete session;
    session= 0;
  }
};

class MultiMessageTask : public OnMessageTask {
  std::tuple<uint8_t,uint32_t,std::vector<char>> _msg;
  tbb::task* execute();
public:
 MultiMessageTask(Session *session_,
		  std::tuple<uint8_t,uint32_t,std::vector<char>> msg)
   : OnMessageTask( session_ ) {
    _msg= msg;
  }
  ~MultiMessageTask() {}
};

class SetImageChannelsTask : public OnMessageTask {
  std::pair<CARTA::SetImageChannels,uint32_t> _req;
  tbb::task* execute();
public:
 SetImageChannelsTask(Session *session_,
		      std::pair<CARTA::SetImageChannels,uint32_t> req_ )
       : OnMessageTask( session_ ) {
    _req= req_;
  }
  ~SetImageChannelsTask() {}
};


class SetImageViewTask : public OnMessageTask {
  int _file_id;
  tbb::task* execute();
 public:
 SetImageViewTask(Session *session_, int fd ) : OnMessageTask( session_ ) {
    _file_id= fd;
  }
  ~SetImageViewTask() {}
};


class SetCursorTask : public OnMessageTask {
  int _file_id;
  tbb::task* execute();
public:
 SetCursorTask(Session *session_ , int fd ) : OnMessageTask( session_ ) {
    _file_id= fd;
  }
  ~SetCursorTask() {}
};


class SetHistogramReqsTask : public OnMessageTask {
  std::tuple<uint8_t,uint32_t,std::vector<char>> _msg;
  tbb::task* execute();
  
public:
 SetHistogramReqsTask(Session *session_,
		      std::tuple<uint8_t,uint32_t,std::vector<char>> msg )
   : OnMessageTask( session_ ) {
    _msg= msg;
  }
  ~SetHistogramReqsTask() {
  }
};

