#ifndef __DB_CONNECT__
#define __DB_CONNECT__

#include <carta-protobuf/register_viewer.pb.h>
#include <carta-protobuf/user_preferences.pb.h>

extern void ConnectToMongoDB();
extern bool SaveLayoutToDB(const std::string& name, const std::string& json_string);
// extern bool SaveUserPreferencesToDB( google::protobuf::Map<std::string,std::string> & map );
extern bool SaveUserPreferencesToDB(const CARTA::SetUserPreferences& request);

extern bool GetLayoutsFromDB(CARTA::RegisterViewerAck* layouts);
extern bool GetPreferencesFromDB(CARTA::RegisterViewerAck* prefs);

#endif // __DB_CONNECT__
