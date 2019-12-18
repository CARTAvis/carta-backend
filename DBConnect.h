#ifndef __DB_CONNECT__
#define __DB_CONNECT__

#include <carta-protobuf/user_preferences.pb.h>

extern void ConnectToMongoDB();
extern bool SaveLayoutToDB(const std::string& name, const std::string& json_string);
// extern bool SaveUserPreferencesToDB( google::protobuf::Map<std::string,std::string> & map );
extern bool SaveUserPreferencesToDB(const CARTA::SetUserPreferences& request);
extern bool GetLayoutsAndProfilesFromDB(
    std::vector<std::tuple<std::string, std::string> >* layouts, std::vector<std::tuple<std::string, std::string> >* profiles);

#endif // __DB_CONNECT__
