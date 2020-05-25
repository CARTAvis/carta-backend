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
        if (casacore::ImageFITSConverter::ImageToFITS(message, *image, output_filename)) {
            success = true;
        }
    } else if ((CasacoreImageType(filename) == casacore::ImageOpener::FITS) && (output_file_type == CARTA::FileType::CASA)) {
        // FITS to CASA conversion
        casacore::ImageInterface<casacore::Float>* fits_to_image_ptr = 0;
        if (casacore::ImageFITSConverter::FITSToImage(fits_to_image_ptr, message, output_filename, filename)) {
            success = true;
        }
        // Without this deletion the output CASA image directory lacks "table.f0" and "table.info" files
        delete fits_to_image_ptr;
    } else if ((CasacoreImageType(filename) == casacore::ImageOpener::AIPSPP) && (output_file_type == CARTA::FileType::CASA)) {
        // Change CASA name
        if (!IsSameFileName(filename, output_filename)) {
            std::unique_ptr<casacore::PagedImage<float>> out_image;
            out_image.reset(new casacore::PagedImage<float>(filename));
            out_image->rename(output_filename);
        } else {
            message = "Same file will not be overridden!";
        }
    } else {
        message = "No saving file action!";
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