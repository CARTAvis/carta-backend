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

    // Set the full file name of the new saving image
    if (directory.find("../") == std::string::npos) {
        output_filename = _root_folder + "/" + directory + "/" + output_filename;
    } else {
        message = "Invalid directory request!";
        save_file_ack.set_success(success);
        save_file_ack.set_message(message);
        return;
    }

    if ((CasacoreImageType(filename) == casacore::ImageOpener::AIPSPP) && (output_file_type == CARTA::FileType::FITS)) {
        // CASA to FITS conversion
        AddSuffix(output_filename, CARTA::FileType::FITS);
        if (casacore::ImageFITSConverter::ImageToFITS(message, *image, output_filename)) {
            success = true;
        }
    } else if ((CasacoreImageType(filename) == casacore::ImageOpener::FITS) && (output_file_type == CARTA::FileType::CASA)) {
        // FITS to CASA conversion
        AddSuffix(output_filename, CARTA::FileType::CASA);
        casacore::ImageInterface<casacore::Float>* fits_to_image_ptr = 0;
        if (casacore::ImageFITSConverter::FITSToImage(fits_to_image_ptr, message, output_filename, filename)) {
            success = true;
        }
        // without this deletion the output CASA image directory lacks "table.f0" and "table.info" files
        delete fits_to_image_ptr;
    } else {
        message = "No file format conversion!";
    }

    save_file_ack.set_success(success);
    RemoveRootFolder(message);
    save_file_ack.set_message(message);
}

void FilesManager::RemoveRootFolder(std::string& directory) {
    if (!_root_folder.empty() && directory.find(_root_folder) == 0) {
        directory.replace(0, _root_folder.size(), "");
    }
}

void FilesManager::AddSuffix(std::string& output_filename, CARTA::FileType file_type) {
    std::size_t found = output_filename.find_last_not_of(".");
    if (found) {
        std::string suffix_name = output_filename.substr(found + 1);
        switch (file_type) {
            case CARTA::FileType::CASA: {
                if (suffix_name != ".image") {
                    output_filename += ".image";
                }
                break;
            }
            case CARTA::FileType::FITS: {
                if (suffix_name != ".fits") {
                    output_filename += ".fits";
                }
                break;
            }
            default: {
            }
        }
    } else {
        switch (file_type) {
            case CARTA::FileType::CASA: {
                output_filename += ".image";
                break;
            }
            case CARTA::FileType::FITS: {
                output_filename += ".fits";
                break;
            }
            default: {
            }
        }
    }
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