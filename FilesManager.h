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

enum class ConversionType { CASA_TO_FITS = 0, FITS_TO_CASA = 1, CASA_TO_CASA = 2, TEMP_TO_CASA = 3, TEMP_TO_FITS = 4, UNKNOWN = 5 };

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
    ConversionType GetConversionType(const std::string& in_file, const CARTA::FileType& out_file_type);
    void RemoveRootFolder(std::string& directory);
    bool IsSameFileName(std::string& in_file, std::string& out_file);
    void GetAbsFileName(const std::string& filename, std::string& path, std::string& name);

    std::string _root_folder;
};

} // namespace carta

#endif // CARTA_BACKEND__FILESMANAGER_H_
