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
        // Without this deletion the output CASA image directory lacks "table.f0" and "table.info" files
        delete fits_to_image_ptr;
    } else if ((CasacoreImageType(filename) == casacore::ImageOpener::AIPSPP) && (output_file_type == CARTA::FileType::CASA)) {
        if (!IsSameFileName(filename, output_filename)) {
            std::unique_ptr<casacore::PagedImage<float>> out_image;
            out_image.reset(new casacore::PagedImage<float>(filename));
            out_image->rename(output_filename);
        } else {
            message = "Same file will not be overridden!";
        }
    } else {
        if (!IsSameFileName(filename, output_filename)) {
#ifdef __linux__
            message = "No saving file action!";
#elif __APPLE__
            fs::rename(filename, output_filename);
            message = "No file format conversion! Rename the file with different name or path.";
#endif
        } else {
            message = "Same file will not be overridden!";
        }
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
    switch (file_type) {
        case CARTA::FileType::CASA: {
            output_filename += ".image";
            break;
        }
        case CARTA::FileType::FITS: {
            output_filename += ".fits";
            break;
        }
        default: {}
    }
}

bool FilesManager::IsSameFileName(const std::string& filename1, const std::string& filename2) {
    bool result(true);
    // Get the absolute file name and path (remove the symbolic link if any)
    casacore::File temp_filename1(filename1);
    casacore::String resolved_filename1 = temp_filename1.path().resolvedName();
    casacore::File temp_filename2(filename2);
    casacore::String resolved_filename2 = temp_filename2.path().resolvedName();
    if (resolved_filename1.size() > resolved_filename2.size()) {
        if (resolved_filename1.find(resolved_filename2) == std::string::npos) {
            result = false;
        }
    } else {
        if (resolved_filename2.find(resolved_filename1) == std::string::npos) {
            result = false;
        }
    }
    return result;
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