#include "FileSettings.h"
#include "OnMessageTask.h"
#include "Session.h"
#include "EventMappings.h"
#include "util.h"
#include <unordered_map>

#include <casacore/casa/OS/HostInfo.h>
#include <casacore/casa/Inputs/Input.h>
#include <fmt/format.h>
#include <tbb/concurrent_queue.h>
#include <tbb/concurrent_unordered_map.h>
#include <tbb/task_scheduler_init.h>
#include <uWS/uWS.h>

#include <fstream>
#include <iostream>
#include <regex>
#include <tuple>
#include <vector>
#include <signal.h>

#include <thread>
#include <mutex>


using namespace std;

// key is uuid:
using key_type = string;

// key is current folder
unordered_map<std::string, vector<string>> permissionsMap;

int sessionNumber;
uWS::Hub wsHub;

// Number of active sessions
int _num_sessions= 0;

// Map from string uuids to 32 bit ints.
unordered_map<std::string,uint8_t> _event_name_map;

// command-line arguments
string rootFolder("/"), baseFolder("."), version_id("1.0.1");
bool verbose, usePermissions;

bool checkRootBaseFolders(std::string& root, std::string& base) {
    if (root=="base" && base == "root") {
        fmt::print("ERROR: Must set root or base directory.\n");
        fmt::print("Exiting carta.\n");
        return false;
    }
    if (root=="base") root = base;
    if (base=="root") base = root;

    // check root
    casacore::File rootFolder(root);
    if (!(rootFolder.exists() && rootFolder.isDirectory(true) && 
          rootFolder.isReadable() && rootFolder.isExecutable())) {
        fmt::print("ERROR: Invalid root directory, does not exist or is not a readable directory.\n");
        fmt::print("Exiting carta.\n");
        return false;
    }
    // absolute path: resolve symlinks, relative paths, env vars e.g. $HOME
    try {
        root = rootFolder.path().resolvedName(); // fails on root folder /
    } catch (casacore::AipsError& err) {
        try {
            root = rootFolder.path().absoluteName();
        } catch (casacore::AipsError& err) {
            fmt::print(err.getMesg());
        }
        if (root.empty()) root = "/";
    }
    // check base
    casacore::File baseFolder(base);
    if (!(baseFolder.exists() && baseFolder.isDirectory(true) && 
          baseFolder.isReadable() && baseFolder.isExecutable())) {
        fmt::print("ERROR: Invalid base directory, does not exist or is not a readable directory.\n");
        fmt::print("Exiting carta.\n");
        return false;
    }
    // absolute path: resolve symlinks, relative paths, env vars e.g. $HOME
    try {
        base = baseFolder.path().resolvedName(); // fails on root folder /
    } catch (casacore::AipsError& err) {
        try {
            base = baseFolder.path().absoluteName();
        } catch (casacore::AipsError& err) {
            fmt::print(err.getMesg());
        }
        if (base.empty()) base = "/";
    }
    // check if base is same as or subdir of root
    if (base != root) {
        bool isSubdirectory(false);
        casacore::Path basePath(base);
        casacore::String parentString(basePath.dirName()), rootString(root);
	if (parentString == rootString)
            isSubdirectory = true;
        while (!isSubdirectory && (parentString != rootString)) {  // navigate up directory tree
            basePath = casacore::Path(parentString);
            parentString = basePath.dirName();
            if (parentString == rootString) {
                isSubdirectory = true;
	    } else if (parentString == "/") {
                break;
            }
        }
        if (!isSubdirectory) {
            fmt::print("ERROR: Base {} must be a subdirectory of root {}. Exiting carta.\n", base, root);
            return false;
        }
    }
    return true;
}

// Reads a permissions file to determine which API keys are required to access various subdirectories
void readPermissions(string filename) {
    ifstream permissionsFile(filename);
    if (permissionsFile.good()) {
        fmt::print("Reading permissions file\n");
        string line;
        regex commentRegex("\\s*#.*");
        regex folderRegex("\\s*(\\S+):\\s*");
        regex keyRegex("\\s*(\\S{4,}|\\*)\\s*");
        string currentFolder;
        while (getline(permissionsFile, line)) {
            smatch matches;
            if (regex_match(line, commentRegex)) {
                continue;
            } else if (regex_match(line, matches, folderRegex) && matches.size() == 2) {
                currentFolder = matches[1].str();
            } else if (currentFolder.length() && regex_match(line, matches, keyRegex) && matches.size() == 2) {
                string key = matches[1].str();
                permissionsMap[currentFolder].push_back(key);
            }
        }
    } else {
        fmt::print("Missing permissions file\n");
    }
}


inline uint8_t get_event_id_by_string(std::string& strname)
{
  int8_t ret= _event_name_map[ strname ];
  if( ! ret ) {
    std::cerr << "Name lookup failure in  get_event_no_by_string : "
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

    ws->setUserData(session);
    outgoing->setData(session);
    
    outgoing->start(
        [](uS::Async *async) -> void {
	  Session * sess = ((Session*)async->getData());
	  sess->sendPendingMessages();
        });

    session= new Session(ws, uuid, permissionsMap, usePermissions,
			 rootFolder, baseFolder, outgoing, verbose);

    ws->setUserData(session);
    outgoing->setData(session);

    log(uuid, "Client {} [{}] Connected. Clients: {}", uuid, ws->getAddress().address, ++_num_sessions);
}



// Called on disconnect. Cleans up sessions. In future, we may want to delay this (in case of unintentional disconnects)
void onDisconnect(uWS::WebSocket<uWS::SERVER>* ws, int code, char* message, size_t length) {
  Session * session= (Session*)ws->getUserData();
  if (session) delete session;
  ws->setUserData(0); //Avoid having destructor called twice.
  --_num_sessions;
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
	session->image_channel_lock();
	if( ! session->image_channel_task_test_and_set() ) 
	  tsk= new (tbb::task::allocate_root()) SetImageChannelsTask( session );
	message.ParseFromArray(eventPayload.data(), eventPayload.size());
	// has its own queue to keep channels in order during animation
	session->addToAniQueue(message, requestId);
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
    if (std::strncmp(rawMessage, "PING", 4) == 0) {
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

	populate_event_name_map();
        }

        if (!checkRootBaseFolders(rootFolder, baseFolder)) {
            return 1;
        }

        // Construct task scheduler, permissions
        tbb::task_scheduler_init task_sched(threadCount);
        if (usePermissions) {
            readPermissions("permissions.txt");
        }

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
