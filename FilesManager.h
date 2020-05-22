#ifndef CARTA_BACKEND__FILESMANAGER_H_
#define CARTA_BACKEND__FILESMANAGER_H_

#include <carta-protobuf/moment_request.pb.h>
#include <carta-protobuf/save_file.pb.h>
#include <casacore/images/Images/ImageFITSConverter.h>

#include <iostream>
#include <iterator>
#include <set>
#include <string>
#include <unordered_map>

#include "Util.h"

namespace carta {

class FilesManager {
public:
    FilesManager(std::string root_folder);
    ~FilesManager();

    void SaveFile(std::string filename, casacore::ImageInterface<float>* image, const CARTA::SaveFile& save_file_msg,
        CARTA::SaveFileAck& save_file_ack);

    // Print protobuf messages
    static void Print(CARTA::SaveFile message);
    static void Print(CARTA::SaveFileAck message);

private:
    void RemoveRootFolder(std::string& directory);
    void AddSuffix(std::string& output_filename, CARTA::FileType file_type);
    std::string _root_folder;
};

} // namespace carta

#endif // CARTA_BACKEND__FILESMANAGER_H_
