#ifndef CARTA_BACKEND_MOMENT_MOMENTFILESMANAGER_H_
#define CARTA_BACKEND_MOMENT_MOMENTFILESMANAGER_H_

#include <carta-protobuf/moment_request.pb.h>

#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace carta {

class MomentFilesManager {
public:
    MomentFilesManager(std::string root_folder);
    ~MomentFilesManager();

    void CacheMomentFiles(CARTA::MomentResponse message);

private:
    std::string _root_folder;
    std::unordered_map<std::string, std::vector<std::string>> _moment_file_directories; // <directory, filenames>
};

} // namespace carta

#endif // CARTA_BACKEND_MOMENT_MOMENTFILESMANAGER_H_
