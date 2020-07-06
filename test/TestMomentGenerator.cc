#include <casacore/images/Images/ImageFITSConverter.h>

#include <iostream>
#include <memory>

#include "../FileConverter.h"
#include "../Moment/MomentGenerator.h"

const std::string FITS_FILE_FULL_NAME = "images/test-moments/HD163296_CO_2_1.image.fits";
// const std::string CASA_FILE_FULL_NAME = "images/test-moments/M17_SWex.image";
const std::string CASA_FILE_FULL_NAME = "images/test-moments/HD163296_CO_2_1.image";
const std::string TEMP_FOLDER = "images/test-moments/temp";
const std::string OUT_CASA_FILE_FULL_NAME = "images/test-moments/temp/HD163296_CO_2_1.image.fits.moment.image";
const std::string OUT_FITS_FILE_FULL_NAME = "images/test-moments/temp/HD163296_CO_2_1.image.fits.moment.fits";

void ConvertFITStoCASA();
void ConvertCASAtoFITS();
void FileManagerConvertFITStoCASA();
void FileManagerConvertCASAtoFITS();
void TestCASAtoCASA();
void TestFITStoFITS();

int main(int argc, char* argv[]) {
    int test_case;
    cout << "Choose a test case:" << endl;
    cout << "    3) Convert FITS to CASA" << endl;
    cout << "    4) Convert CASA to FITS" << endl;
    cout << "    7) FileManager converts FITS to CASA" << endl;
    cout << "    8) FileManager converts CASA to FITS" << endl;
    cout << "   12) Test CASA to CASA" << endl;
    cout << "   13) Test FITS to FITS" << endl;
    cin >> test_case;

    switch (test_case) {
        case 3:
            ConvertFITStoCASA();
            break;
        case 4:
            ConvertCASAtoFITS();
            break;
        case 7:
            FileManagerConvertFITStoCASA();
            break;
        case 8:
            FileManagerConvertCASAtoFITS();
            break;
        case 12:
            TestCASAtoCASA();
            break;
        case 13:
            TestFITStoFITS();
            break;
        default:
            cout << "No such test case!" << endl;
            break;
    }

    return 0;
}

void ConvertFITStoCASA() {
    // A FITS to CASA image conversion
    String output_image_file_full_name = "images/test-moments/HD163296_CO_2_1.image"; // Set the output full name of CASA image

    // Remove the old output file if exists
    casacore::File cc_file(output_image_file_full_name);
    if (cc_file.exists()) {
        system(("rm -rf " + output_image_file_full_name).c_str());
    }

    // Do conversion
    String error;
    ImageInterface<Float>* fits_to_image_ptr = 0;
    Bool ok = ImageFITSConverter::FITSToImage(fits_to_image_ptr, error, output_image_file_full_name, FITS_FILE_FULL_NAME);

    if (!ok) {
        std::cerr << "Fail to convert FITS to CASA image!\n";
        std::cerr << error << std::endl;
    }

    delete fits_to_image_ptr;
}

void ConvertCASAtoFITS() {
    // A CASA image to FITS conversion
    String output_fits_file_full_name = "images/test-moments/M17_SWex.fits"; // Set the output full name of FITS image

    // Remove the old output file if exists
    casacore::File cc_file(output_fits_file_full_name);
    if (cc_file.exists()) {
        system(("rm -f " + output_fits_file_full_name).c_str());
    }

    // Do conversion
    PagedImage<Float>* image = new casacore::PagedImage<Float>(CASA_FILE_FULL_NAME);
    String error;
    Bool ok = ImageFITSConverter::ImageToFITS(error, *image, output_fits_file_full_name);

    if (!ok) {
        std::cerr << "Fail to convert CASA image to FITS!\n";
        std::cerr << error << std::endl;
    }

    delete image;
}

void FileManagerConvertFITStoCASA() {
    // Set saving file message
    CARTA::SaveFile save_file_msg;
    save_file_msg.set_file_id(-1);
    save_file_msg.set_output_file_name("HD163296_CO_2_1.image");
    save_file_msg.set_output_file_directory("/images/test-moments");
    save_file_msg.set_output_file_type(CARTA::FileType::CASA);

    std::unique_ptr<casacore::FITSImage> image = std::make_unique<casacore::FITSImage>(FITS_FILE_FULL_NAME);

    // Response message
    CARTA::SaveFileAck save_file_ack;
    carta::FileConverter moment_files_manager("./");
    moment_files_manager.SaveFile(FITS_FILE_FULL_NAME, image.get(), save_file_msg, save_file_ack);

    std::cout << "==========================================" << std::endl;
    carta::FileConverter::Print(save_file_msg);
    std::cout << "==========================================" << std::endl;
    carta::FileConverter::Print(save_file_ack);
}

void FileManagerConvertCASAtoFITS() {
    // Set saving file message
    CARTA::SaveFile save_file_msg;
    save_file_msg.set_file_id(-1);
    save_file_msg.set_output_file_name("M17_SWex.fits");
    save_file_msg.set_output_file_directory("/images/test-moments");
    save_file_msg.set_output_file_type(CARTA::FileType::FITS);

    // Set the CASA image file pointer with the original file name
    auto image = std::make_unique<casacore::PagedImage<casacore::Float>>(CASA_FILE_FULL_NAME);

    // Response message
    CARTA::SaveFileAck save_file_ack;
    carta::FileConverter moment_files_manager("./");
    moment_files_manager.SaveFile(CASA_FILE_FULL_NAME, image.get(), save_file_msg, save_file_ack);

    std::cout << "==========================================" << std::endl;
    carta::FileConverter::Print(save_file_msg);
    std::cout << "==========================================" << std::endl;
    carta::FileConverter::Print(save_file_ack);
}

void TestCASAtoCASA() {
    // Open a CASA image file
    auto image = std::make_unique<casacore::PagedImage<float>>(CASA_FILE_FULL_NAME);

    CARTA::SaveFile save_file_msg;
    save_file_msg.set_file_id(-1);
    save_file_msg.set_output_file_name("M17_SWex.image.copy");
    save_file_msg.set_output_file_directory("images/test-moments");
    save_file_msg.set_output_file_type(CARTA::FileType::CASA);

    // Response message
    CARTA::SaveFileAck save_file_ack;

    carta::FileConverter moment_files_manager("./");
    moment_files_manager.SaveFile(CASA_FILE_FULL_NAME, image.get(), save_file_msg, save_file_ack);

    std::cout << "==========================================" << std::endl;
    carta::FileConverter::Print(save_file_msg);
    std::cout << "==========================================" << std::endl;
    carta::FileConverter::Print(save_file_ack);
}

void TestFITStoFITS() {
    // Open a FITS image file
    auto image = std::make_unique<casacore::FITSImage>(FITS_FILE_FULL_NAME);

    CARTA::SaveFile save_file_msg;
    save_file_msg.set_file_id(-1);
    save_file_msg.set_output_file_name("HD163296_CO_2_1.image.fits.copy");
    save_file_msg.set_output_file_directory("images/test-moments");
    save_file_msg.set_output_file_type(CARTA::FileType::FITS);

    // Response message
    CARTA::SaveFileAck save_file_ack;

    carta::FileConverter moment_files_manager("./");
    moment_files_manager.SaveFile(FITS_FILE_FULL_NAME, image.get(), save_file_msg, save_file_ack);

    std::cout << "==========================================" << std::endl;
    carta::FileConverter::Print(save_file_msg);
    std::cout << "==========================================" << std::endl;
    carta::FileConverter::Print(save_file_ack);
}
