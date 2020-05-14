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
            std::string remove_file = "rm -rf " + full_filename;
            const char* command = remove_file.c_str();
            system(command);
        }
    }
}

void FilesManager::SaveFile(
    std::string filename, casacore::ImageInterface<float>* image, const CARTA::SaveFile& save_file_msg, CARTA::SaveFileAck& save_file_ack) {
    int file_id(save_file_msg.file_id());
    std::string output_file_name(save_file_msg.output_file_name());
    CARTA::FileType output_file_type(save_file_msg.output_file_type());

    // Get the full file name of the new saving image
    std::size_t found = filename.find_last_of("/");
    std::string directory = filename.substr(0, found);
    output_file_name = directory + "/" + output_file_name;

    // Remove the old output file if exists
    casacore::File cc_file(output_file_name);
    if (cc_file.exists() && (filename != output_file_name)) {
        system(("rm -rf " + output_file_name).c_str());
    }

    // Set response message
    save_file_ack.set_file_id(file_id);

    if ((CasacoreImageType(filename) == casacore::ImageOpener::AIPSPP) && (output_file_type == CARTA::FileType::FITS)) {
        // CASA image to FITS conversion
        casacore::String error;
        casacore::Bool ok = casacore::ImageFITSConverter::ImageToFITS(error, *image, output_file_name);
        if (!ok) {
            save_file_ack.set_success(false);
            save_file_ack.set_message(error);
        } else {
            save_file_ack.set_success(true);
        }
    } else if ((CasacoreImageType(filename) == casacore::ImageOpener::FITS) && (output_file_type == CARTA::FileType::CASA)) {
        // FITS to CASA image conversion
        casacore::String error;
        casacore::ImageInterface<casacore::Float>* fits_to_image_ptr = 0;
        casacore::Bool ok = casacore::ImageFITSConverter::FITSToImage(fits_to_image_ptr, error, output_file_name, filename);

        delete fits_to_image_ptr; // without this deletion the output image directory lacks "table.f0" and "table.info" files

        if (!ok) {
            save_file_ack.set_success(false);
            save_file_ack.set_message(error);
        } else {
            save_file_ack.set_success(true);
        }
    } else {
        std::string command = "cp -a " + filename + " " + output_file_name;
        std::string message = "No file format conversion!";
        if (filename != output_file_name) {
            system(command.c_str());
        }
        save_file_ack.set_success(true);
        save_file_ack.set_message(message);
    }
}

// Print protobuf messages
void FilesManager::Print(CARTA::SaveFile message) {
    std::cout << "CARTA::SaveFile:" << std::endl;
    std::cout << "file_id = " << message.file_id() << std::endl;
    std::cout << "output_file_name = " << message.output_file_name() << std::endl;
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