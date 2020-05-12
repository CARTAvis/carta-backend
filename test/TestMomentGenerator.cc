#include <casacore/images/Images/ImageFITSConverter.h>

#include <iostream>

#include "../Moment/MomentFilesManager.h"
#include "../Moment/MomentGenerator.h"

void Test1();
void Test2();
void Test3();
void Test4();

int main(int argc, char* argv[]) {
    int test_case;
    cout << "Choose a test case:" << endl;
    cout << "    1) Test1()" << endl;
    cout << "    2) Test2()" << endl;
    cout << "    3) Test3()" << endl;
    cout << "    4) Test4()" << endl;
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
        default:
            cout << "No such test case!" << endl;
            break;
    }

    return 0;
}

void Test1() {
    // Set the image file name
    std::string filename = "images/test-moments/HD163296_CO_2_1.image.fits";

    // Open a FITS image file
    std::unique_ptr<casacore::FITSImage> image;
    image.reset(new casacore::FITSImage(filename));

    // Print the original image file info
    std::cout << "file name: " << filename << std::endl;
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
        filename, image.get(), "", spectral_axis, stokes_axis, moment_request, moment_response, progress_callback);

    // Print protobuf messages
    std::cout << "==========================================" << std::endl;
    carta::MomentGenerator::Print(moment_request);
    std::cout << "==========================================" << std::endl;
    carta::MomentGenerator::Print(moment_response);

    // Moment files manager
    carta::MomentFilesManager moment_files_manager("./");
    moment_files_manager.CacheMomentFiles(moment_response);
}

void Test2() {
    // Set the image file name
    std::string filename = "images/test-moments/M17_SWex.image";

    // Open a CASA image file
    std::unique_ptr<casacore::PagedImage<float>> image;
    image.reset(new casacore::PagedImage<float>(filename));

    // Print the original image file info
    std::cout << "file name: " << filename << std::endl;
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
        filename, image.get(), "", spectral_axis, stokes_axis, moment_request, moment_response, progress_callback);

    // Print protobuf messages
    std::cout << "==========================================" << std::endl;
    carta::MomentGenerator::Print(moment_request);
    std::cout << "==========================================" << std::endl;
    carta::MomentGenerator::Print(moment_response);

    // Moment files manager
    carta::MomentFilesManager moment_files_manager("./");
    moment_files_manager.CacheMomentFiles(moment_response);
}

void Test3() {
    // A FITS to CASA image conversion
    ImageInterface<Float>* fits_to_image_ptr = 0;
    if (std::__fs::filesystem::exists("images/test-moments/HD163296_CO_2_1.image")) {
        system("rm -rf images/test-moments/HD163296_CO_2_1.image");
    }
    String fits_filename = "images/test-moments/HD163296_CO_2_1.image.fits";    // An existing FITS image
    String output_image_filename = "images/test-moments/HD163296_CO_2_1.image"; // Set the output full name of CASA image
    String error;
    Bool ok = ImageFITSConverter::FITSToImage(fits_to_image_ptr, error, output_image_filename, fits_filename);
    if (!ok) {
        std::cerr << "Fail to convert FITS to CASA image!\n";
        std::cerr << error << std::endl;
    }
    delete fits_to_image_ptr;
}

void Test4() {
    // A CASA image to FITS conversion
    if (std::__fs::filesystem::exists("images/test-moments/M17_SWex.fits")) {
        system("rm -f images/test-moments/M17_SWex.fits");
    }
    String image_filename = "images/test-moments/M17_SWex.image"; // An existing CASA image
    PagedImage<Float>* image = new casacore::PagedImage<Float>(image_filename);
    String output_fits_filename = "images/test-moments/M17_SWex.fits"; // Set the output full name of FITS image
    String error;
    Bool ok = ImageFITSConverter::ImageToFITS(error, *image, output_fits_filename);
    if (!ok) {
        std::cerr << "Fail to convert CASA image to FITS!\n";
        std::cerr << error << std::endl;
    }
    delete image;
}
