#include "FilesManager.h"

using namespace carta;

FilesManager::FilesManager(std::string root_folder) : _root_folder(root_folder) {}

FilesManager::~FilesManager() {}

void FilesManager::SaveFile(
    std::string filename, casacore::ImageInterface<float>* image, const CARTA::SaveFile& save_file_msg, CARTA::SaveFileAck& save_file_ack) {
    int file_id(save_file_msg.file_id());
    std::string output_filename(save_file_msg.output_file_name());
    std::string directory(save_file_msg.output_file_directory());
    CARTA::FileType output_file_type(save_file_msg.output_file_type());

    // Set response message
    save_file_ack.set_file_id(file_id);
    bool success = false;
    casacore::String message;

    // Get the full name of the output image
    std::string temp_path = _root_folder + "/" + directory;
    casacore::File cc_path(temp_path);
    casacore::String resolved_path = cc_path.path().resolvedName();
    std::string abs_path = resolved_path;
    output_filename = abs_path + "/" + output_filename;

    ConversionType conversion_type = GetConversionType(filename, output_file_type);

    switch (conversion_type) {
        case ConversionType::CASA_TO_FITS: {
            // Allow to overwrite the same output file name
            if (casacore::ImageFITSConverter::ImageToFITS(message, *image, output_filename, 64, casacore::True, casacore::True, -32, 1.0,
                    -1.0, casacore::True, casacore::False, casacore::True, casacore::False, casacore::False, casacore::False,
                    casacore::String(), casacore::True)) {
                success = true;
            }
            break;
        }
        case ConversionType::FITS_TO_CASA: {
            // Allow to overwrite the same output file name
            casacore::ImageInterface<casacore::Float>* fits_to_image_ptr = 0;
            if (casacore::ImageFITSConverter::FITSToImage(
                    fits_to_image_ptr, message, output_filename, filename, 0, 0, 64, casacore::True, casacore::False)) {
                success = true;
            }
            // Without this deletion the output CASA image directory lacks "table.f0" and "table.info" files
            delete fits_to_image_ptr;
            break;
        }
        case ConversionType::CASA_TO_CASA: {
            // Change the CASA image name
            if (!IsSameFileName(filename, output_filename)) {
                std::unique_ptr<casacore::PagedImage<float>> out_image;
                out_image.reset(new casacore::PagedImage<float>(filename));
                out_image->rename(output_filename);
                success = true;
            } else {
                message = "Same file will not be overwritten!";
            }
            break;
        }
        default: {
            message = "No saving file action!";
            break;
        }
    }

    save_file_ack.set_success(success);
    RemoveRootFolder(message);
    save_file_ack.set_message(message);
}

ConversionType FilesManager::GetConversionType(const std::string& in_file, const CARTA::FileType& out_file_type) {
    ConversionType result(ConversionType::UNKNOWN);
    if (!in_file.empty()) {
        if ((CasacoreImageType(in_file) == casacore::ImageOpener::AIPSPP) && (out_file_type == CARTA::FileType::FITS)) {
            result = ConversionType::CASA_TO_FITS;
        } else if ((CasacoreImageType(in_file) == casacore::ImageOpener::FITS) && (out_file_type == CARTA::FileType::CASA)) {
            result = ConversionType::FITS_TO_CASA;
        } else if ((CasacoreImageType(in_file) == casacore::ImageOpener::AIPSPP) && (out_file_type == CARTA::FileType::CASA)) {
            result = ConversionType::CASA_TO_CASA;
        }
    } else {
        if (out_file_type == CARTA::FileType::CASA) {
            result = ConversionType::TEMP_TO_CASA;
        } else if (out_file_type == CARTA::FileType::FITS) {
            result = ConversionType::TEMP_TO_FITS;
        }
    }
    return result;
}

void FilesManager::RemoveRootFolder(std::string& directory) {
    if (!_root_folder.empty() && directory.find(_root_folder) == 0) {
        directory.replace(0, _root_folder.size(), "");
    }
}

bool FilesManager::IsSameFileName(std::string& in_file, std::string& out_file) {
    bool result(true);
    // Get the absolute file name and path (remove the symbolic link if any)
    std::string abs_in_file_path;
    std::string in_file_name;
    GetAbsFileName(in_file, abs_in_file_path, in_file_name);

    std::string abs_out_file_path;
    std::string out_file_name;
    GetAbsFileName(out_file, abs_out_file_path, out_file_name);

    if ((abs_in_file_path != abs_out_file_path) || (in_file_name != out_file_name)) {
        result = false;
    }

    in_file = abs_in_file_path + "/" + in_file_name;
    out_file = abs_out_file_path + "/" + out_file_name;

    return result;
}

void FilesManager::GetAbsFileName(const std::string& filename, std::string& path, std::string& name) {
    size_t found = filename.find_last_of("/");
    name = filename.substr(found + 1);
    std::string temp_path = filename.substr(0, found);
    casacore::File cc_path(temp_path);
    casacore::String resolved_path = cc_path.path().resolvedName();
    path = resolved_path;
}

// Print protobuf messages

void FilesManager::Print(CARTA::SaveFile message) {
    std::cout << "CARTA::SaveFile:" << std::endl;
    std::cout << "file_id = " << message.file_id() << std::endl;
    std::cout << "output_file_name = " << message.output_file_name() << std::endl;
    std::cout << "output_file_directory = " << message.output_file_directory() << std::endl;
    if (message.output_file_type() == CARTA::FileType::CASA) {
        std::cout << "output_file_type = CASA" << std::endl;
    } else if (message.output_file_type() == CARTA::FileType::FITS) {
        std::cout << "output_file_type = FITS" << std::endl;
    } else {
        std::cout << "output_file_type = Unknown!" << std::endl;
    }
}

void FilesManager::Print(CARTA::SaveFileAck message) {
    std::cout << "CARTA::SaveFileAck:" << std::endl;
    std::cout << "file_id = " << message.file_id() << std::endl;
    if (message.success()) {
        std::cout << "success = true" << std::endl;
    } else {
        std::cout << "success = false" << std::endl;
    }
    std::cout << "message = " << message.message() << std::endl;
}