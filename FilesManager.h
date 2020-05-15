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

    void CacheMomentTempFiles(CARTA::MomentResponse message);
    void RemoveMomentTempFiles();
    void CheckMomentFileName(std::string output_filename);
    void SaveFile(std::string filename, casacore::ImageInterface<float>* image, const CARTA::SaveFile& save_file_msg,
        CARTA::SaveFileAck& save_file_ack);

    // Print protobuf messages
    static void Print(CARTA::SaveFile message);
    static void Print(CARTA::SaveFileAck message);

private:
    bool IsSameFile(std::string filename1, std::string filename2);
    void RemoveRootFolder(std::string& directory);
    std::string _root_folder;
    std::unordered_map<std::string, std::set<std::string>> _moment_file_directories; // <directory, filenames>
};

} // namespace carta

#endif // CARTA_BACKEND__FILESMANAGER_H_
