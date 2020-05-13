#ifndef CARTA_BACKEND_MOMENT_MOMENTFILESMANAGER_H_
#define CARTA_BACKEND_MOMENT_MOMENTFILESMANAGER_H_

#include <carta-protobuf/moment_request.pb.h>
#include <carta-protobuf/save_file.pb.h>
#include <casacore/images/Images/ImageFITSConverter.h>

#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "../Util.h"

namespace carta {

class MomentFilesManager {
public:
    MomentFilesManager(std::string root_folder);
    ~MomentFilesManager();

    void CacheMomentFiles(CARTA::MomentResponse message);
    void SaveMomentFile(std::string filename, casacore::ImageInterface<float>* image, const CARTA::SaveFile& save_file_msg,
        CARTA::SaveFileAck& save_file_ack);

    // Print protobuf messages
    static void Print(CARTA::SaveFile message);
    static void Print(CARTA::SaveFileAck message);

private:
    std::string _root_folder;
    std::unordered_map<std::string, std::vector<std::string>> _moment_file_directories; // <directory, filenames>
};

} // namespace carta

#endif // CARTA_BACKEND_MOMENT_MOMENTFILESMANAGER_H_
