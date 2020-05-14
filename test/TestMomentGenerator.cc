#include <casacore/images/Images/ImageFITSConverter.h>

#include <iostream>

#include "../Moment/MomentFilesManager.h"
#include "../Moment/MomentGenerator.h"

const std::string fits_file_full_name = "images/test-moments/HD163296_CO_2_1.image.fits";
const std::string image_file_full_name = "images/test-moments/M17_SWex.image";

void Test1();
void Test2();
void Test3();
void Test4();
void Test5();
void Test6();
void Test7();
void Test8();

int main(int argc, char* argv[]) {
    int test_case;
    cout << "Choose a test case:" << endl;
    cout << "    1) Test1()" << endl;
    cout << "    2) Test2()" << endl;
    cout << "    3) Test3()" << endl;
    cout << "    4) Test4()" << endl;
    cout << "    5) Test5()" << endl;
    cout << "    6) Test6()" << endl;
    cout << "    7) Test7()" << endl;
    cout << "    8) Test8()" << endl;
    cin >> test_case;

    switch (test_case) {
        case 1:
            Test1();
            break;
        case 2:
            Test2();
            break;
        case 3:
            Test3();
            break;
        case 4:
            Test4();
            break;
        case 5:
            Test5();
            break;
        case 6:
            Test6();
            break;
        case 7:
            Test7();
            break;
        case 8:
            Test8();
            break;
        default:
            cout << "No such test case!" << endl;
            break;
    }

    return 0;
}

void Test1() {
    // Open a FITS image file
    std::unique_ptr<casacore::FITSImage> image;
    image.reset(new casacore::FITSImage(fits_file_full_name));

    // Print the original image file info
    std::cout << "file name: " << fits_file_full_name << std::endl;
    std::cout << "in_image.shape().size(): " << image->shape().size() << std::endl;
    std::cout << "in_image.shape().nelements(): " << image->shape().nelements() << std::endl;
    for (int i = 0; i < image->shape().size(); ++i) {
        std::cout << "in_image.shape()[" << i << "]= " << image->shape()[i] << std::endl;
    }

    // Get spectral and stokes indices
    casacore::CoordinateSystem coord_sys = image->coordinates();
    casacore::Vector<casacore::Int> linear_axes = coord_sys.linearAxesNumbers();
    int spectral_axis = coord_sys.spectralAxisNumber();
    int stokes_axis = coord_sys.polarizationAxisNumber();
    std::cout << "spectral_axis = " << spectral_axis << std::endl;
    std::cout << "stokes_axis = " << stokes_axis << std::endl;

    // Set moment request message
    CARTA::MomentRequest moment_request;
    moment_request.set_file_id(-1);
    moment_request.set_region_id(-1);

    moment_request.add_moments(CARTA::Moment::MEAN_OF_THE_SPECTRUM);
    moment_request.add_moments(CARTA::Moment::INTEGRATED_OF_THE_SPECTRUM);
    moment_request.add_moments(CARTA::Moment::INTENSITY_WEIGHTED_COORD);
    moment_request.add_moments(CARTA::Moment::INTENSITY_WEIGHTED_DISPERSION_OF_THE_COORD);
    moment_request.add_moments(CARTA::Moment::MEDIAN_OF_THE_SPECTRUM);
    // moment_request.add_moments(CARTA::Moment::MEDIAN_COORDINATE);
    moment_request.add_moments(CARTA::Moment::STD_ABOUT_THE_MEAN_OF_THE_SPECTRUM);
    moment_request.add_moments(CARTA::Moment::RMS_OF_THE_SPECTRUM);
    moment_request.add_moments(CARTA::Moment::ABS_MEAN_DEVIATION_OF_THE_SPECTRUM);
    moment_request.add_moments(CARTA::Moment::MAX_OF_THE_SPECTRUM);
    moment_request.add_moments(CARTA::Moment::COORD_OF_THE_MAX_OF_THE_SPECTRUM);
    moment_request.add_moments(CARTA::Moment::MIN_OF_THE_SPECTRUM);
    moment_request.add_moments(CARTA::Moment::COORD_OF_THE_MIN_OF_THE_SPECTRUM);

    moment_request.set_axis(CARTA::MomentAxis::SPECTRAL);
    auto spectral_range = moment_request.mutable_spectral_range();
    spectral_range->set_min(0);
    spectral_range->set_max(249);
    moment_request.set_mask(CARTA::MomentMask::None);
    auto pixel_range = moment_request.mutable_pixel_range();
    pixel_range->set_min(0.0);
    pixel_range->set_max(100.0);

    // Moment response
    CARTA::MomentResponse moment_response;

    // Set moment progress callback function
    auto progress_callback = [&](float progress) {
        CARTA::MomentProgress moment_progress;
        moment_progress.set_progress(progress);
        std::cout << "==========================================" << std::endl;
        carta::MomentGenerator::Print(moment_progress);
    };

    // Calculate moments
    carta::MomentGenerator moment_generator(
        fits_file_full_name, image.get(), "", spectral_axis, stokes_axis, moment_request, moment_response, progress_callback);

    // Print protobuf messages
    std::cout << "==========================================" << std::endl;
    carta::MomentGenerator::Print(moment_request);
    std::cout << "==========================================" << std::endl;
    carta::MomentGenerator::Print(moment_response);

    // Moment files manager
    // carta::MomentFilesManager moment_files_manager("./");
    // moment_files_manager.CacheMomentFiles(moment_response);
}

void Test2() {
    // Open a CASA image file
    std::unique_ptr<casacore::PagedImage<float>> image;
    image.reset(new casacore::PagedImage<float>(image_file_full_name));

    // Print the original image file info
    std::cout << "file name: " << image_file_full_name << std::endl;
    std::cout << "in_image.shape().size(): " << image->shape().size() << std::endl;
    std::cout << "in_image.shape().nelements(): " << image->shape().nelements() << std::endl;
    for (int i = 0; i < image->shape().size(); ++i) {
        std::cout << "in_image.shape()[" << i << "]= " << image->shape()[i] << std::endl;
    }

    // Get spectral and stokes indices
    casacore::CoordinateSystem coord_sys = image->coordinates();
    casacore::Vector<casacore::Int> linear_axes = coord_sys.linearAxesNumbers();
    int spectral_axis = coord_sys.spectralAxisNumber();
    int stokes_axis = coord_sys.polarizationAxisNumber();
    std::cout << "spectral_axis = " << spectral_axis << std::endl;
    std::cout << "stokes_axis = " << stokes_axis << std::endl;

    // Set moment request message
    CARTA::MomentRequest moment_request;
    moment_request.set_file_id(-1);
    moment_request.set_region_id(-1);

    moment_request.add_moments(CARTA::Moment::MEAN_OF_THE_SPECTRUM);
    moment_request.add_moments(CARTA::Moment::INTEGRATED_OF_THE_SPECTRUM);
    moment_request.add_moments(CARTA::Moment::INTENSITY_WEIGHTED_COORD);
    moment_request.add_moments(CARTA::Moment::INTENSITY_WEIGHTED_DISPERSION_OF_THE_COORD);
    moment_request.add_moments(CARTA::Moment::MEDIAN_OF_THE_SPECTRUM);
    // moment_request.add_moments(CARTA::Moment::MEDIAN_COORDINATE);
    moment_request.add_moments(CARTA::Moment::STD_ABOUT_THE_MEAN_OF_THE_SPECTRUM);
    moment_request.add_moments(CARTA::Moment::RMS_OF_THE_SPECTRUM);
    moment_request.add_moments(CARTA::Moment::ABS_MEAN_DEVIATION_OF_THE_SPECTRUM);
    moment_request.add_moments(CARTA::Moment::MAX_OF_THE_SPECTRUM);
    moment_request.add_moments(CARTA::Moment::COORD_OF_THE_MAX_OF_THE_SPECTRUM);
    moment_request.add_moments(CARTA::Moment::MIN_OF_THE_SPECTRUM);
    moment_request.add_moments(CARTA::Moment::COORD_OF_THE_MIN_OF_THE_SPECTRUM);

    moment_request.set_axis(CARTA::MomentAxis::SPECTRAL);
    auto spectral_range = moment_request.mutable_spectral_range();
    spectral_range->set_min(0);
    spectral_range->set_max(10);
    moment_request.set_mask(CARTA::MomentMask::None);
    auto pixel_range = moment_request.mutable_pixel_range();
    pixel_range->set_min(0.0);
    pixel_range->set_max(100.0);

    // Moment response
    CARTA::MomentResponse moment_response;

    // Set moment progress callback function
    auto progress_callback = [&](float progress) {
        CARTA::MomentProgress moment_progress;
        moment_progress.set_progress(progress);
        std::cout << "==========================================" << std::endl;
        carta::MomentGenerator::Print(moment_progress);
    };

    // Calculate moments
    carta::MomentGenerator moment_generator(
        image_file_full_name, image.get(), "", spectral_axis, stokes_axis, moment_request, moment_response, progress_callback);

    // Print protobuf messages
    std::cout << "==========================================" << std::endl;
    carta::MomentGenerator::Print(moment_request);
    std::cout << "==========================================" << std::endl;
    carta::MomentGenerator::Print(moment_response);

    // Moment files manager
    // carta::MomentFilesManager moment_files_manager("./");
    // moment_files_manager.CacheMomentFiles(moment_response);
}

void Test3() {
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
    Bool ok = ImageFITSConverter::FITSToImage(fits_to_image_ptr, error, output_image_file_full_name, fits_file_full_name);

    if (!ok) {
        std::cerr << "Fail to convert FITS to CASA image!\n";
        std::cerr << error << std::endl;
    }

    delete fits_to_image_ptr;
}

void Test4() {
    // A CASA image to FITS conversion
    String output_fits_file_full_name = "images/test-moments/M17_SWex.fits"; // Set the output full name of FITS image

    // Remove the old output file if exists
    casacore::File cc_file(output_fits_file_full_name);
    if (cc_file.exists()) {
        system(("rm -f " + output_fits_file_full_name).c_str());
    }

    // Do conversion
    PagedImage<Float>* image = new casacore::PagedImage<Float>(image_file_full_name);
    String error;
    Bool ok = ImageFITSConverter::ImageToFITS(error, *image, output_fits_file_full_name);

    if (!ok) {
        std::cerr << "Fail to convert CASA image to FITS!\n";
        std::cerr << error << std::endl;
    }

    delete image;
}

void Test5() {
    // Create moments from the FITS file
    Test1();

    // Set saving file message
    CARTA::SaveFile save_file_msg;
    save_file_msg.set_file_id(-1);
    save_file_msg.set_output_file_name("test.fits");
    save_file_msg.set_output_file_type(CARTA::FileType::FITS);

    // Set the moment image file (as CASA format)
    string original_moment_file_name = fits_file_full_name + ".moment.average";
    PagedImage<Float>* image = new casacore::PagedImage<Float>(original_moment_file_name);

    // Response message
    CARTA::SaveFileAck save_file_ack;
    carta::MomentFilesManager moment_files_manager("./");
    moment_files_manager.SaveMomentFile(original_moment_file_name, image, save_file_msg, save_file_ack);

    std::cout << "==========================================" << std::endl;
    carta::MomentFilesManager::Print(save_file_msg);
    std::cout << "==========================================" << std::endl;
    carta::MomentFilesManager::Print(save_file_ack);

    delete image;
}

void Test6() {
    // Create moments from the CASA file
    Test2();

    // Set saving file message
    CARTA::SaveFile save_file_msg;
    save_file_msg.set_file_id(-1);
    save_file_msg.set_output_file_name("test.image");
    save_file_msg.set_output_file_type(CARTA::FileType::CASA);

    // Set the moment image file (as CASA format)
    string original_moment_file_name = image_file_full_name + ".moment.average";
    PagedImage<Float>* image = new casacore::PagedImage<Float>(original_moment_file_name);

    // Response message
    CARTA::SaveFileAck save_file_ack;
    carta::MomentFilesManager moment_files_manager("./");
    moment_files_manager.SaveMomentFile(original_moment_file_name, image, save_file_msg, save_file_ack);

    std::cout << "==========================================" << std::endl;
    carta::MomentFilesManager::Print(save_file_msg);
    std::cout << "==========================================" << std::endl;
    carta::MomentFilesManager::Print(save_file_ack);

    delete image;
}

void Test7() {
    // FITS file to CASA image conversion:

    // Set saving file message
    CARTA::SaveFile save_file_msg;
    save_file_msg.set_file_id(-1);
    save_file_msg.set_output_file_name("HD163296_CO_2_1.image");
    save_file_msg.set_output_file_type(CARTA::FileType::CASA);

    // This pointer will not be used
    ImageInterface<Float>* image = 0;

    // Response message
    CARTA::SaveFileAck save_file_ack;
    carta::MomentFilesManager moment_files_manager("./");
    moment_files_manager.SaveMomentFile(fits_file_full_name, image, save_file_msg, save_file_ack);

    std::cout << "==========================================" << std::endl;
    carta::MomentFilesManager::Print(save_file_msg);
    std::cout << "==========================================" << std::endl;
    carta::MomentFilesManager::Print(save_file_ack);

    delete image;
}

void Test8() {
    // CASA image to FITS conversion:

    // Set saving file message
    CARTA::SaveFile save_file_msg;
    save_file_msg.set_file_id(-1);
    save_file_msg.set_output_file_name("M17_SWex.fits");
    save_file_msg.set_output_file_type(CARTA::FileType::FITS);

    // Set the CASA image file pointer with the original file name
    PagedImage<Float>* image = new casacore::PagedImage<Float>(image_file_full_name);

    // Response message
    CARTA::SaveFileAck save_file_ack;
    carta::MomentFilesManager moment_files_manager("./");
    moment_files_manager.SaveMomentFile(image_file_full_name, image, save_file_msg, save_file_ack);

    std::cout << "==========================================" << std::endl;
    carta::MomentFilesManager::Print(save_file_msg);
    std::cout << "==========================================" << std::endl;
    carta::MomentFilesManager::Print(save_file_ack);

    delete image;
}