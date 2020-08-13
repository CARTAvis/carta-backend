#include "FileConverter.h"

#ifdef _BOOST_FILESYSTEM_
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

using namespace carta;

FileConverter::FileConverter(std::string root_folder) : _root_folder(root_folder) {}

void FileConverter::SaveFile(const std::string& in_file, casacore::ImageInterface<float>* image, const CARTA::SaveFile& save_file_msg,
    CARTA::SaveFileAck& save_file_ack) {
    int file_id(save_file_msg.file_id());
    std::string output_filename(save_file_msg.output_file_name());
    std::string directory(save_file_msg.output_file_directory());
    CARTA::FileType output_file_type(save_file_msg.output_file_type());

    // Set response message
    save_file_ack.set_file_id(file_id);
    bool success = false;
    casacore::String message;

    // Get the full resolved name of the output image
    std::string temp_path = _root_folder + "/" + directory;
    std::string abs_path = fs::absolute(temp_path);
    output_filename = abs_path + "/" + output_filename;

    if (output_filename == in_file) {
        message = "The source file can not be overwritten!";
        save_file_ack.set_success(success);
        save_file_ack.set_message(message);
        return;
    }

    // Check the writing permission
    fs::path tmp_dir = fs::path(output_filename).parent_path();
    fs::perms tmp_perm = fs::status(tmp_dir).permissions();
    if (!fs::exists(tmp_dir) || ((tmp_perm & fs::perms::owner_write) == fs::perms::none)) {
        message = "No write permission!";
        save_file_ack.set_success(success);
        save_file_ack.set_message(message);
        return;
    }

    if (output_file_type == CARTA::FileType::CASA) {
        // Remove the old image file if it has a same file name
        if (fs::exists(output_filename)) {
            fs::remove_all(output_filename);
        }

        // Get a copy of the original pixel data
        casacore::IPosition start(image->shape().size(), 0);
        casacore::IPosition count(image->shape());
        casacore::Slicer slice(start, count);
        casacore::Array<casacore::Float> temp_array;
        image->doGetSlice(temp_array, slice);

        // Construct a new CASA image
        auto out_image = std::make_unique<casacore::PagedImage<casacore::Float>>(image->shape(), image->coordinates(), output_filename);
        out_image->setMiscInfo(image->miscInfo());
        out_image->setImageInfo(image->imageInfo());
        out_image->appendLog(image->logger());
        out_image->setUnits(image->units());
        out_image->putSlice(temp_array, start);

        // Copy the mask if the original image has
        if (image->hasPixelMask()) {
            casacore::Array<casacore::Bool> image_mask;
            image->getMaskSlice(image_mask, slice);
            out_image->makeMask("mask0", true, true);
            casacore::Lattice<casacore::Bool>& out_image_mask = out_image->pixelMask();
            out_image_mask.putSlice(image_mask, start);
        }
        success = true;

    } else if (output_file_type == CARTA::FileType::FITS) {
        // Remove the old image file if it has a same file name
        casacore::Bool ok = casacore::ImageFITSConverter::ImageToFITS(message, *image, output_filename, 64, casacore::True, casacore::True,
            -32, 1.0, -1.0, casacore::True, casacore::False, casacore::True, casacore::False, casacore::False, casacore::False,
            casacore::String(), casacore::True);
        if (ok) {
            success = true;
        }

    } else {
        message = "No saving file action!";
    }

    save_file_ack.set_success(success);
    RemoveRootFolder(message);
    save_file_ack.set_message(message);
}

void FileConverter::RemoveRootFolder(std::string& directory) {
    if (!_root_folder.empty() && directory.find(_root_folder) == 0) {
        directory.replace(0, _root_folder.size(), "");
    }
}
