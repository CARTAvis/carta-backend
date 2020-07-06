#ifndef CARTA_BACKEND__FILESMANAGER_H_
#define CARTA_BACKEND__FILESMANAGER_H_

#include <carta-protobuf/moment_request.pb.h>
#include <carta-protobuf/save_file.pb.h>
#include <casacore/images/Images/ImageFITSConverter.h>

#include <filesystem>

namespace fs = std::filesystem;

namespace carta {

class FileConverter {
public:
    FileConverter(std::string root_folder);
    ~FileConverter(){};

    void SaveFile(const std::string& in_file, casacore::ImageInterface<float>* image, const CARTA::SaveFile& save_file_msg,
        CARTA::SaveFileAck& save_file_ack);

private:
    void RemoveRootFolder(std::string& directory);
    std::string _root_folder;
};

} // namespace carta

#endif // CARTA_BACKEND__FILESMANAGER_H_
