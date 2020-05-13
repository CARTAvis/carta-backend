#include "MomentFilesManager.h"

using namespace carta;

MomentFilesManager::MomentFilesManager(std::string root_folder) : _root_folder(root_folder) {}

MomentFilesManager::~MomentFilesManager() {
    // Remove the moment files while deleted
    for (auto& moment_file_directory : _moment_file_directories) {
        const std::string& directory = moment_file_directory.first;
        const std::vector<std::string>& filenames = moment_file_directory.second;
        for (const auto& filename : filenames) {
            std::string full_filename = _root_folder + directory + "/" + filename;
            std::string remove_file = "rm -rf " + full_filename;
            const char* command = remove_file.c_str();
            system(command);
        }
    }
}

void MomentFilesManager::CacheMomentFiles(CARTA::MomentResponse message) {
    std::string directory(message.directory());
    for (int i = 0; i < message.output_files_size(); ++i) {
        std::string filename(message.output_files(i).file_name());
        _moment_file_directories[directory].push_back(filename);
    }
}

void MomentFilesManager::SaveMomentFile(std::string filename, casacore::ImageInterface<float>* image,
    const CARTA::SaveMomentFile& save_moment_file_msg, CARTA::SaveMomentFileAck& save_moment_file_ack) {
    int file_id(save_moment_file_msg.file_id());
    std::string output_file_name(save_moment_file_msg.output_file_name());
    CARTA::FileType output_file_type(save_moment_file_msg.output_file_type());

    std::size_t found = filename.find_last_of("/");
    std::string directory = filename.substr(0, found);
    output_file_name = directory + "/" + output_file_name;

    // Remove the old output file if exists
    casacore::File cc_file(output_file_name);
    if (cc_file.exists()) {
        system(("rm -f " + output_file_name).c_str());
    }

    // Set response message
    save_moment_file_ack.set_file_id(file_id);

    if (CasacoreImageType(filename) == casacore::ImageOpener::AIPSPP) { // Make sure the moment file is a CASA image
        if (output_file_type == CARTA::FileType::FITS) {                // Convert the CASA image to FITS
            casacore::String error;
            casacore::Bool ok = casacore::ImageFITSConverter::ImageToFITS(error, *image, output_file_name);
            if (!ok) {
                save_moment_file_ack.set_success(false);
                save_moment_file_ack.set_message(error);
            } else {
                save_moment_file_ack.set_success(true);
            }
        } else if (output_file_type == CARTA::FileType::CASA) { // Change the name of CASA image file
            std::string command = "cp -a " + filename + " " + output_file_name;
            system(command.c_str());
            save_moment_file_ack.set_success(true);
        } else {
            std::string error = "Unknown converting image type!";
            save_moment_file_ack.set_success(false);
            save_moment_file_ack.set_message(error);
        }
    } else {
        std::string error = "Not a CASA image as the moment image type!";
        save_moment_file_ack.set_success(false);
        save_moment_file_ack.set_message(error);
    }
}

// Print protobuf messages
void MomentFilesManager::Print(CARTA::SaveMomentFile message) {
    std::cout << "CARTA::SaveMomentFile:" << std::endl;
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

void MomentFilesManager::Print(CARTA::SaveMomentFileAck message) {
    std::cout << "CARTA::SaveMomentFileAck:" << std::endl;
    std::cout << "file_id = " << message.file_id() << std::endl;
    if (message.success()) {
        std::cout << "success = true" << std::endl;
    } else {
        std::cout << "success = false" << std::endl;
    }
    std::cout << "message = " << message.message() << std::endl;
}