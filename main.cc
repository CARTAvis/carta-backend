#include <casacore/casa/OS/HostInfo.h>
#include <casacore/casa/Inputs/Input.h>
#include <fmt/format.h>
#include <tbb/concurrent_queue.h>
#include <tbb/concurrent_unordered_map.h>
#include <tbb/task_scheduler_init.h>
#include <uWS/uWS.h>

#include <iostream>
#include <tuple>
#include <vector>
#include <signal.h>

#include <thread>
#include <mutex>

#include "EventMappings.h"
#include "FileListHandler.h"
#include "FileSettings.h"
#include "OnMessageTask.h"
#include "Session.h"
#include "util.h"

using namespace std;

// key is uuid:
using key_type = string;

// key is current folder
unordered_map<string, vector<string>> permissionsMap;

// file list handler for the file browser
FileListHandler* fileListHandler;

int sessionNumber;
uWS::Hub wsHub;

// Map from string uuids to 8 bit ints.
unordered_map<std::string,uint8_t> _event_name_map;

// command-line arguments
string rootFolder("/"), baseFolder("."), version_id("1.1");
bool verbose, usePermissions;



inline uint8_t get_event_id_by_string(string& strname)
{
  int8_t ret= _event_name_map[ strname ];
  if( ! ret ) {
    cerr << "Name lookup failure in  get_event_no_by_string : "
	      << strname << endl;
  }
  return ret;
}



// Called on connection. Creates session objects and assigns UUID and API keys to it
void onConnect(uWS::WebSocket<uWS::SERVER>* ws, uWS::HttpRequest httpRequest) {
    string uuidstr = fmt::format("{}{}", ++sessionNumber,
        casacore::Int(casacore::HostInfo::secondsFrom1970()));

    auto &uuid= *((string *)new string(uuidstr));

    uS::Async *outgoing = new uS::Async(wsHub.getLoop());

    Session *session;

    outgoing->start(
        [](uS::Async *async) -> void {
	  Session * sess = ((Session*)async->getData());
	  sess->sendPendingMessages();
        });


    session= new Session(ws, uuid, rootFolder, outgoing, fileListHandler, verbose);

    ws->setUserData(session);
    session->increase_ref_count();
    outgoing->setData(session);

    log(uuid, "Client {} [{}] Connected. Num sessions: {}",
	uuid, ws->getAddress().address, Session::number_of_sessions());
}



// Called on disconnect. Cleans up sessions. In future, we may want to delay this (in case of unintentional disconnects)
void onDisconnect(uWS::WebSocket<uWS::SERVER>* ws, int code,
		  char* message, size_t length) {
  Session * session= (Session*)ws->getUserData();
  
  if( session ) {
    auto uuid= session->uuid;
    session->disconnect_called();
    log(uuid, "Client {} [{}] Disconnected. Remaining sessions: {}",
	uuid, ws->getAddress().address, Session::number_of_sessions());
    if ( ! session->decrease_ref_count() ) {
      delete session;
      ws->setUserData(nullptr);
    }
  }
  else {
    cerr <<
      "Warning: OnDisconnect called with no Session object.\n";
  }
}





// Forward message requests to session callbacks after parsing message into relevant ProtoBuf message
void onMessage(uWS::WebSocket<uWS::SERVER>* ws, char* rawMessage,
	       size_t length, uWS::OpCode opCode) {
  Session * session= (Session*) ws->getUserData();
  if (!session) {
    fmt::print("Missing session!\n");
    return;
  }

  if (opCode == uWS::OpCode::BINARY) {
    if (length > 36) {
      static const size_t max_len = 32;
      string eventName(rawMessage, min(strlen(rawMessage), max_len));
      uint32_t requestId = *reinterpret_cast<uint32_t*>(rawMessage+32);
      vector<char> eventPayload(&rawMessage[36], &rawMessage[length]);
      
      OnMessageTask * tsk= nullptr;
      uint8_t event_id= get_event_id_by_string( eventName );

      switch(event_id) {
      case 0: {
	// Do nothing if event type is not known.
	break;
      }
      case SET_IMAGE_CHANNELS_ID: {
	CARTA::SetImageChannels message;
	message.ParseFromArray(eventPayload.data(), eventPayload.size());
	session->image_channel_lock();
	if( ! session->image_channel_task_test_and_set() ) {
	  tsk= new (tbb::task::allocate_root())
	    SetImageChannelsTask(session, make_pair(message, requestId));
	}
	else {
	// has its own queue to keep channels in order during animation
	  session->addToAniQueue(message, requestId);
	}
	session->image_channel_unlock();
	break;
      }
      case SET_IMAGE_VIEW_ID: {
        CARTA::SetImageView message;
        message.ParseFromArray(eventPayload.data(), eventPayload.size());
        session->addViewSetting(message, requestId);
        tsk= new (tbb::task::allocate_root())
	  SetImageViewTask(session, message.file_id());
	break;
      }	
      case SET_CURSOR_ID: {
	CARTA::SetCursor message;
        message.ParseFromArray(eventPayload.data(), eventPayload.size());
        session->addCursorSetting(message, requestId);
        tsk= new (tbb::task::allocate_root()) SetCursorTask(session, message.file_id());
	break;
      }
      case SET_HISTOGRAM_REQUIREMENTS_ID: {
        CARTA::SetHistogramRequirements message;
        message.ParseFromArray(eventPayload.data(), eventPayload.size());
        if(message.histograms_size() == 0) {
          session->cancel_SetHistReqs();
        }
        else {
          tsk= new (tbb::task::allocate_root())
	    SetHistogramReqsTask(session,
				 make_tuple(event_id,
					    requestId,
					    eventPayload));
        }
	break;
      }
      case START_ANIMATION_ID: {
	CARTA::StartAnimation message;
	message.ParseFromArray(eventPayload.data(), eventPayload.size());
	tsk= new (tbb::task::allocate_root())
	  AnimationTask(session, requestId, message );
	break;
      }
      case STOP_ANIMATION_ID: {
	CARTA::StopAnimation message;
	message.ParseFromArray(eventPayload.data(), eventPayload.size());
	session->stop_animation( message.file_id(), message.end_frame() );
	break;
      }
      default: {
	tsk= new (tbb::task::allocate_root())
	  MultiMessageTask(session,
			   make_tuple(event_id,
				      requestId,
				      eventPayload) );
      }
      }
      if( tsk ) tbb::task::enqueue(*tsk);
    }
  }
  else if (opCode == uWS::OpCode::TEXT) {
    if (strncmp(rawMessage, "PING", 4) == 0) {
      ws->send("PONG");
    }
  }
}




void exit_backend(int s) {
    fmt::print("Exiting backend.\n");
    exit(0);
}





// Note : this is still under construction.
void populate_event_name_map(void)
{
  _event_name_map["REGISTER_VIEWER"]= REGISTER_VIEWER_ID;
  _event_name_map["FILE_LIST_REQUEST"]= FILE_LIST_REQUEST_ID;
  _event_name_map["FILE_INFO_REQUEST"]= FILE_INFO_REQUEST_ID;;
  _event_name_map["OPEN_FILE"]= OPEN_FILE_ID;
  _event_name_map["CLOSE_FILE"]= CLOSE_FILE_ID;
  _event_name_map["SET_IMAGE_VIEW"]= SET_IMAGE_VIEW_ID;
  _event_name_map["SET_IMAGE_CHANNELS"]= SET_IMAGE_CHANNELS_ID;
  _event_name_map["SET_CURSOR"]= SET_CURSOR_ID;
  _event_name_map["SET_SPATIAL_REQUIREMENTS"]= SET_SPATIAL_REQUIREMENTS_ID;
  _event_name_map["SET_SPECTRAL_REQUIREMENTS"]= SET_SPECTRAL_REQUIREMENTS_ID;
  _event_name_map["SET_HISTOGRAM_REQUIREMENTS"]= SET_HISTOGRAM_REQUIREMENTS_ID;
  _event_name_map["SET_STATS_REQUIREMENTS"]= SET_STATS_REQUIREMENTS_ID;
  _event_name_map["SET_REGION"]= SET_REGION_ID;
  _event_name_map["REMOVE_REGION"]= REMOVE_REGION_ID;
  _event_name_map["START_ANIMATION"]= START_ANIMATION_ID;
  _event_name_map["STOP_ANIMATION"]= STOP_ANIMATION_ID;
}


  
// Entry point. Parses command line arguments and starts server listening
int main(int argc, const char* argv[]) {
    try {
        // set up interrupt signal handler
        struct sigaction sigHandler;
        sigHandler.sa_handler = exit_backend;
        sigemptyset(&sigHandler.sa_mask);
        sigHandler.sa_flags = 0;
        sigaction(SIGINT, &sigHandler, nullptr);

        // define and get input arguments
        int port(3002);
        int threadCount(tbb::task_scheduler_init::default_num_threads());
        { // get values then let Input go out of scope
        casacore::Input inp;
        inp.version(version_id);
        inp.create("verbose", "False", "display verbose logging", "Bool");
        inp.create("permissions", "False", "use a permissions file for determining access", "Bool");
        inp.create("port", to_string(port), "set server port", "Int");
        inp.create("threads", to_string(threadCount), "set thread pool count", "Int");
        inp.create("base", baseFolder, "set folder for data files", "String");
        inp.create("root", rootFolder, "set top-level folder for data files", "String");
        inp.readArguments(argc, argv);

        verbose = inp.getBool("verbose");
        usePermissions = inp.getBool("permissions");
        port = inp.getInt("port");
        threadCount = inp.getInt("threads");
        baseFolder = inp.getString("base");
        rootFolder = inp.getString("root");

        }

        if (!checkRootBaseFolders(rootFolder, baseFolder)) {
            return 1;
        }

	// Used to map between sting names of messages and local int ids.
	populate_event_name_map();

        // Construct task scheduler, permissions
        tbb::task_scheduler_init task_sched(threadCount);
        if (usePermissions) {
            readPermissions("permissions.txt", permissionsMap);
        }

	// One filelisthandler works for all sessions.
	fileListHandler =
	  new FileListHandler(permissionsMap, usePermissions,
			      rootFolder, baseFolder);

        sessionNumber = 0;

        wsHub.onMessage(&onMessage);
        wsHub.onConnection(&onConnect);
        wsHub.onDisconnection(&onDisconnect);
        if (wsHub.listen(port)) {
            fmt::print("Listening on port {} with root folder {}, base folder {}, and {} threads in thread pool\n", port, rootFolder, baseFolder, threadCount);
            wsHub.run();
        } else {
            fmt::print("Error listening on port {}\n", port);
            return 1;
        }
    }
    catch (exception& e) {
        fmt::print("Error: {}\n", e.what());
        return 1;
    }
    catch (...) {
        fmt::print("Unknown error\n");
        return 1;
    }
    return 0;
}
