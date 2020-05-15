#include "FilesManager.h"

using namespace carta;

FilesManager::FilesManager(std::string root_folder) : _root_folder(root_folder) {}

FilesManager::~FilesManager() {
    // Remove moment temp files while deleted
    RemoveMomentTempFiles();
}

void FilesManager::CacheMomentTempFiles(CARTA::MomentResponse message) {
    std::string directory(message.directory());
    for (int i = 0; i < message.output_files_size(); ++i) {
        std::string filename(message.output_files(i).file_name());
        _moment_file_directories[directory].insert(filename);
    }
}

void FilesManager::RemoveMomentTempFiles() {
    for (auto& moment_file_directory : _moment_file_directories) {
        const std::string& directory = moment_file_directory.first;
        const std::set<std::string>& filenames = moment_file_directory.second;
        for (const auto& filename : filenames) {
            std::string full_filename = _root_folder + directory + "/" + filename;
            casacore::File cc_file(full_filename);
            if (cc_file.exists()) {
                std::string remove_file = "rm -rf " + full_filename;
                const char* command = remove_file.c_str();
                system(command);
            }
        }
    }
}

void FilesManager::CheckMomentFileName(std::string output_filename) {
    // Remove the moment file name if it is in the cache of temp moment file names,
    // in order to prevent deleting such file while close the session
    for (auto& moment_file_directory : _moment_file_directories) {
        const std::string& directory = moment_file_directory.first;
        std::set<std::string>& filenames = moment_file_directory.second;
        for (const auto& filename : filenames) {
            std::string full_filename = _root_folder + directory + "/" + filename;
            if (IsSameFile(filename, output_filename)) {
                filenames.erase(filename);
            }
        }
    }
}

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
        output_filename = _root_folder + directory + "/" + output_filename;
    } else {
        message = "Invalid directory request!";
        save_file_ack.set_success(success);
        save_file_ack.set_message(message);
        return;
    }

    if ((CasacoreImageType(filename) == casacore::ImageOpener::AIPSPP) && (output_file_type == CARTA::FileType::FITS)) {
        // CASA image to FITS conversion
        if (casacore::ImageFITSConverter::ImageToFITS(message, *image, output_filename)) {
            success = true;
        }
    } else if ((CasacoreImageType(filename) == casacore::ImageOpener::FITS) && (output_file_type == CARTA::FileType::CASA)) {
        // FITS to CASA image conversion
        casacore::ImageInterface<casacore::Float>* fits_to_image_ptr = 0;
        if (casacore::ImageFITSConverter::FITSToImage(fits_to_image_ptr, message, output_filename, filename)) {
            success = true;
        }
        // without this deletion the output CASA image directory lacks "table.f0" and "table.info" files
        delete fits_to_image_ptr;
    } else {
        if (!IsSameFile(filename, output_filename)) {
            message = "No file format conversion!";
            std::string command = "cp -a " + filename + " " + output_filename;
            system(command.c_str());
            success = true;
        } else {
            message = "File already exists, will not overwrite.";
        }
    }

    save_file_ack.set_success(success);
    RemoveRootFolder(message);
    save_file_ack.set_message(message);

    // Do not remove the file if the saving file name is same with the temporary moment file name
    CheckMomentFileName(output_filename);
}

bool FilesManager::IsSameFile(std::string filename1, std::string filename2) {
    bool result(false);
    std::size_t found;
    if (filename1.size() >= filename2.size()) {
        found = filename1.find(filename2);
    } else {
        found = filename2.find(filename1);
    }
    if (found != std::string::npos) {
        result = true;
    }
    return result;
}

void FilesManager::RemoveRootFolder(std::string& directory) {
    if (!_root_folder.empty() && directory.find(_root_folder) == 0) {
        directory.replace(0, _root_folder.size(), "");
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