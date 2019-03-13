#include "OnMessageTask.h"
#include "util.h"
#include <algorithm>
#include <cstring>
#include <fmt/format.h>
#include "EventMappings.h"

// Looks for null termination in a char array to determine event names from message payloads
std::string getEventName(char* rawMessage) {
    static const size_t max_len = 32;
    return std::string(rawMessage, std::min(std::strlen(rawMessage), max_len));
}

unsigned int __num_on_message_tasks= 0;
unsigned int __on_message_tasks_created= 0;


tbb::task*
MultiMessageTask::execute()
{
  //CARTA ICD
  std::tuple<uint32_t,uint32_t,std::vector<char>> msg;
  session->evtq.try_pop(msg);
  uint32_t event_type;
  uint32_t requestId;
  std::vector<char> eventPayload;
  std::tie(event_type, requestId, eventPayload) = msg;
  
  switch( event_type ) {
  case REGISTER_VIEWER_ID: {
    CARTA::RegisterViewer message;
    if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
      session->onRegisterViewer(message, requestId);
    }
    break;
  }
  case FILE_LIST_REQUEST_ID: {
    CARTA::FileListRequest message;
    if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
      session->onFileListRequest(message, requestId);
    }
    break;
  }
  case FILE_INFO_REQUEST_ID: {
    CARTA::FileInfoRequest message;
    if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
      session->onFileInfoRequest(message, requestId);
    }
    break;
  }
  case OPEN_FILE_ID: {
    CARTA::OpenFile message;
    if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
      session->onOpenFile(message, requestId);
    }
    break;
  }
  case CLOSE_FILE_ID: {
    CARTA::CloseFile message;
    if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
      session->fsettings.clearSettings(message.file_id());
      session->onCloseFile(message, requestId);
    }
    break;
  }
  case SET_SPATIAL_REQUIREMENTS_ID: {
    CARTA::SetSpatialRequirements message;
    if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
      session->onSetSpatialRequirements(message, requestId);
    }
    break;
  }
  case SET_HISTOGRAM_REQUIREMENTS_ID: {
    CARTA::SetHistogramRequirements message;
    if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
      session->onSetHistogramRequirements(message, requestId);
    }
    break;
  }
  case SET_SPECTRAL_REQUIREMENTS_ID: {
    CARTA::SetSpectralRequirements message;
    if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
      session->onSetSpectralRequirements(message, requestId);
    }
    break;
  }
  case SET_STATS_REQUIREMENTS_ID: {
    CARTA::SetStatsRequirements message;
    if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
      session->onSetStatsRequirements(message, requestId);
    }
    break;
  }
  case SET_REGION_ID: {
    CARTA::SetRegion message;
    if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
      session->onSetRegion(message, requestId);
    }
    break;
  }
  case REMOVE_REGION_ID: {
    CARTA::RemoveRegion message;
    if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
      session->onRemoveRegion(message, requestId);
    }
    break;
  }
  default: {
    std::cerr << " Bad event type in MultiMessageType:execute : "
	      << event_type << std::endl;
    exit(1);
  }
  }

  return nullptr;
}

tbb::task*
SetImageChannelsTask::execute()
{
  int tester;

  do {
    session->executeOneAniEvt();
    session->image_channel_lock();
    tester = session->aniq.unsafe_size();
    if( ! tester ) session->image_channal_task_set_idle();
    session->image_channel_unlock();
  } while( tester );
  
  return nullptr;
}


tbb::task*
SetImageViewTask::execute()
{
  std::tuple<uint32_t,uint32_t,std::vector<char>> msg;
  session->evtq.try_pop(msg);
  uint32_t event_type;
  uint32_t requestId;
  std::vector<char> eventPayload;
  std::tie(event_type, requestId, eventPayload) = msg;

  CARTA::SetImageView message;

  if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
    session->fsettings.executeOne("SET_IMAGE_VIEW", message.file_id());
  }
    

  return nullptr;
}

tbb::task*
SetCursorTask::execute()
{
   std::tuple<uint32_t,uint32_t,std::vector<char>> msg;
   session->evtq.try_pop(msg);
   uint32_t event_type;
   uint32_t requestId;
   std::vector<char> eventPayload;
   std::tie(event_type, requestId, eventPayload) = msg;

   CARTA::SetCursor message;
   if (message.ParseFromArray(eventPayload.data(), eventPayload.size())) {
     session->fsettings.executeOne("SET_CURSOR", message.file_id());
   }

   return nullptr;
}
