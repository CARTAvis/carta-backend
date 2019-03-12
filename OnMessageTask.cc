#include "OnMessageTask.h"
#include "util.h"
#include <algorithm>
#include <cstring>
#include <fmt/format.h>

// Looks for null termination in a char array to determine event names from message payloads
std::string getEventName(char* rawMessage) {
    static const size_t max_len = 32;
    return std::string(rawMessage, std::min(std::strlen(rawMessage), max_len));
}

unsigned int __num_on_message_tasks= 0;
unsigned int __on_message_tasks_created= 0;


#include <mutex>          // std::mutex


extern std::mutex sequentialiser;



tbb::task*
OnMessageTask::execute()
{
    //CARTA ICD
    std::tuple<std::string,uint32_t,std::vector<char>> msg;
    session->evtq.try_pop(msg);
    std::string eventName;
    uint32_t requestId;
    std::vector<char> eventPayload;
    std::tie(eventName, requestId, eventPayload) = msg;

    if (eventName == "REGISTER_VIEWER") {
        CARTA::RegisterViewer message;
        if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
	    session->onRegisterViewer(message, requestId);
        }
    } else if (eventName == "FILE_LIST_REQUEST") {
        CARTA::FileListRequest message;
        if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
            session->onFileListRequest(message, requestId);
        }
    } else if (eventName == "FILE_INFO_REQUEST") {
        CARTA::FileInfoRequest message;
        if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
            session->onFileInfoRequest(message, requestId);
        }
    } else if (eventName == "OPEN_FILE") {
        CARTA::OpenFile message;

        if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
            session->onOpenFile(message, requestId);
        }	
    } else if (eventName == "CLOSE_FILE") {
        CARTA::CloseFile message;
        if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
            session->fsettings.clearSettings(message.file_id());
            session->onCloseFile(message, requestId);
        }
  
    } else if (eventName == "SET_SPATIAL_REQUIREMENTS") {
        CARTA::SetSpatialRequirements message;
        if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
            session->onSetSpatialRequirements(message, requestId);
        }
    } else if (eventName == "SET_HISTOGRAM_REQUIREMENTS") {
        CARTA::SetHistogramRequirements message;
        if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
            session->onSetHistogramRequirements(message, requestId);
        }
    } else if (eventName == "SET_SPECTRAL_REQUIREMENTS") {
        CARTA::SetSpectralRequirements message;
        if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
            session->onSetSpectralRequirements(message, requestId);
        }
    } else if (eventName == "SET_STATS_REQUIREMENTS") {
        CARTA::SetStatsRequirements message;
        if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
            session->onSetStatsRequirements(message, requestId);
        }
    } else if (eventName == "SET_REGION") {
        CARTA::SetRegion message;
        if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
            session->onSetRegion(message, requestId);
        }
    } else if (eventName == "REMOVE_REGION") {
        CARTA::RemoveRegion message;
        if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
            session->onRemoveRegion(message, requestId);
        }
    }
    
#ifdef __SEQUENTIAL__
    sequentialiser.unlock();
    std::cerr << " in message - out OMT4\n";
#endif

    return nullptr;
}

tbb::task*
SetImageChannelsTask::execute()
{
  std::tuple<std::string,uint32_t,std::vector<char>> msg;
  bool tester;

  //  cerr << "\n SetImageChannelsTask exec : " << this <<
  //    " : " << session << "\n\n";

  if( ! session ) {
    cerr << " SetImageChannelsTask : No Session pointer : " << this << "\n";
    exit(1);
  }
 
  session->evtq.try_pop(msg);

  do {
    std::string eventName;
    uint32_t requestId;
    std::vector<char> eventPayload;

    std::tie(eventName, requestId, eventPayload) = msg;
  
    CARTA::SetImageChannels message;
    if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
      session->executeOneAniEvt();
    }
   
    session->image_channel_lock();
    tester= session->evtq.try_pop(msg);
    if( ! tester ) session->image_channal_task_set_idle();
    session->image_channel_unlock();
    //    std::cerr << " OSICT Exec tester=" << tester << endl;
  } while( tester );
  
#ifdef __SEQUENTIAL__
  sequentialiser.unlock();
  std::cerr << " in message - out \n";
#endif

  return nullptr;
}


tbb::task*
SetImageViewTask::execute()
{
  std::tuple<std::string,uint32_t,std::vector<char>> msg;
  session->evtq.try_pop(msg);
  std::string eventName;
  uint32_t requestId;
  std::vector<char> eventPayload;
  std::tie(eventName, requestId, eventPayload) = msg;

  //  cerr << " OMT2 exec " << session << "\n";
  CARTA::SetImageView message;

  if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
    session->fsettings.executeOne("SET_IMAGE_VIEW", message.file_id());
  }
    
#ifdef __SEQUENTIAL__
    sequentialiser.unlock();
    std::cerr << " in message - out OMT2\n";
#endif

    return nullptr;
}

tbb::task*
SetCursorTask::execute()
{

  //  cerr << " OMT5 exec " << session << "\n";
   std::tuple<std::string,uint32_t,std::vector<char>> msg;
   session->evtq.try_pop(msg);
   std::string eventName;
   uint32_t requestId;
   std::vector<char> eventPayload;
   std::tie(eventName, requestId, eventPayload) = msg;

   CARTA::SetCursor message;
   if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
     session->fsettings.executeOne("SET_CURSOR", message.file_id());
   }

#ifdef __SEQUENTIAL__
    sequentialiser.unlock();
    std::cerr << " in message - out \n";
#endif

    return nullptr;
}
