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

#include "EventHeader.h"
#include "FileListHandler.h"
#include "FileSettings.h"
#include "OnMessageTask.h"
#include "Session.h"
#include "util.h"

using namespace std;

// key is current folder
unordered_map<string, vector<string>> permissionsMap;

// file list handler for the file browser
FileListHandler* fileListHandler;

uint32_t sessionNumber;
uWS::Hub wsHub;


// command-line arguments
string rootFolder("/"), baseFolder("."), version_id("1.1");
bool verbose, usePermissions;




// Called on connection. Creates session objects and assigns UUID and API keys to it
void onConnect(uWS::WebSocket<uWS::SERVER>* ws, uWS::HttpRequest httpRequest) {
    sessionNumber++;
    // protect against overflow
    sessionNumber = max(sessionNumber, 1u);

    uS::Async *outgoing = new uS::Async(wsHub.getLoop());

    Session *session;

    outgoing->start(
        [](uS::Async *async) -> void {
	  Session * sess = ((Session*)async->getData());
	  sess->sendPendingMessages();
        });


    session= new Session(ws, sessionNumber, rootFolder, outgoing, fileListHandler, verbose);

    ws->setUserData(session);
    session->increase_ref_count();
    outgoing->setData(session);

    log(sessionNumber, "Client {} [{}] Connected. Num sessions: {}",
        sessionNumber, ws->getAddress().address, Session::number_of_sessions());
}



// Called on disconnect. Cleans up sessions. In future, we may want to delay this (in case of unintentional disconnects)
void onDisconnect(uWS::WebSocket<uWS::SERVER>* ws, int code,
		  char* message, size_t length) {
  Session * session= (Session*)ws->getUserData();
  
  if( session ) {
    auto uuid= session->id;
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
            CARTA::EventHeader head= *reinterpret_cast<CARTA::EventHeader*>(rawMessage);
            char * event_buf= rawMessage + sizeof(CARTA::EventHeader);
            int event_length= length - sizeof(CARTA::EventHeader);
            static const size_t max_len = 32;
            OnMessageTask * tsk= nullptr;

            switch(head._type) {
            case CARTA::EventType::SET_IMAGE_CHANNELS: {
                CARTA::SetImageChannels message;
                message.ParseFromArray(event_buf, event_length);
                session->image_channel_lock();
                if( ! session->image_channel_task_test_and_set() ) {
                    tsk= new (tbb::task::allocate_root())
                        SetImageChannelsTask(session, make_pair(message, head._req_id));
                }
                else {
                    // has its own queue to keep channels in order during animation
                    session->addToSetChanQueue(message, head._req_id);
                }
                session->image_channel_unlock();
                break;
            }
            case CARTA::EventType::SET_IMAGE_VIEW: {
                CARTA::SetImageView message;
                message.ParseFromArray(event_buf, event_length);
                session->addViewSetting(message, head._req_id);
                tsk= new (tbb::task::allocate_root())
                    SetImageViewTask(session, message.file_id());
                break;
            }
            case CARTA::EventType::SET_CURSOR: {
                CARTA::SetCursor message;
                message.ParseFromArray(event_buf, event_length);
                session->addCursorSetting(message, head._req_id);
                tsk= new (tbb::task::allocate_root()) SetCursorTask(session, message.file_id());
                break;
            }
	    case CARTA::EventType::SET_HISTOGRAM_REQUIREMENTS: {
                CARTA::SetHistogramRequirements message;
                message.ParseFromArray(event_buf, event_length);
                if(message.histograms_size() == 0) {
                    session->cancel_SetHistReqs();
                }
                else {
                    tsk= new (tbb::task::allocate_root())
                        SetHistogramReqsTask(session, head, event_length, event_buf);
                }
                break;
            }
            case CARTA::EventType::START_ANIMATION: {
                CARTA::StartAnimation message;
                message.ParseFromArray(event_buf, event_length);
                tsk= new (tbb::task::allocate_root())
                    AnimationTask(session, head._req_id, message );
                break;
            }
            case CARTA::EventType::STOP_ANIMATION: {
                CARTA::StopAnimation message;
                message.ParseFromArray(event_buf, event_length);
                session->stop_animation( message.file_id(), message.end_frame() );
                break;
            }
            default: {
                tsk= new (tbb::task::allocate_root())
                    MultiMessageTask(session, head, event_length, event_buf );
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
