#include <casacore/images/Images/ImageFITSConverter.h>

#include <iostream>
#include <memory>

#include "../FilesManager.h"
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
void TestTempImage();
void TestCASAtoCASA();
void TestFITStoFITS();
void TestCalculateMoments();
void TestMedianCoord();

int main(int argc, char* argv[]) {
    int test_case;
    cout << "Choose a test case:" << endl;
    cout << "    1) CalculateMoments()" << endl;
    cout << "    2) TestMedianCoord()" << endl;
    cout << "    3) Convert FITS to CASA" << endl;
    cout << "    4) Convert CASA to FITS" << endl;
    cout << "    7) FileManager converts FITS to CASA" << endl;
    cout << "    8) FileManager converts CASA to FITS" << endl;
    cout << "   10) Test TempImage" << endl;
    cout << "   12) Test CASA to CASA" << endl;
    cout << "   13) Test FITS to FITS" << endl;
    cin >> test_case;

    switch (test_case) {
        case 1:
            TestCalculateMoments();
            break;
        case 2:
            TestMedianCoord();
            break;
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
        case 10:
            TestTempImage();
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
    carta::FilesManager moment_files_manager("./");
    moment_files_manager.SaveFile(FITS_FILE_FULL_NAME, image.get(), save_file_msg, save_file_ack);

    std::cout << "==========================================" << std::endl;
    carta::FilesManager::Print(save_file_msg);
    std::cout << "==========================================" << std::endl;
    carta::FilesManager::Print(save_file_ack);
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
    carta::FilesManager moment_files_manager("./");
    moment_files_manager.SaveFile(CASA_FILE_FULL_NAME, image.get(), save_file_msg, save_file_ack);

    std::cout << "==========================================" << std::endl;
    carta::FilesManager::Print(save_file_msg);
    std::cout << "==========================================" << std::endl;
    carta::FilesManager::Print(save_file_ack);
}

void TestTempImage() {
    // Open a FITS image file
    auto image = std::make_unique<casacore::FITSImage>(FITS_FILE_FULL_NAME);

    // Get spectral and stokes indices
    casacore::CoordinateSystem coord_sys = image->coordinates();
    casacore::Vector<casacore::Int> linear_axes = coord_sys.linearAxesNumbers();
    int spectral_axis = coord_sys.spectralAxisNumber();
    int stokes_axis = coord_sys.polarizationAxisNumber();

    // Set moment request message
    CARTA::MomentRequest moment_request;
    int file_id = 0;
    moment_request.set_file_id(file_id);
    moment_request.set_region_id(-1);
    moment_request.add_moments(CARTA::Moment::INTEGRATED_OF_THE_SPECTRUM);
    moment_request.set_axis(CARTA::MomentAxis::SPECTRAL);
    auto* spectral_range = moment_request.mutable_spectral_range();
    spectral_range->set_min(0);
    spectral_range->set_max(249);
    moment_request.set_mask(CARTA::MomentMask::None);
    auto* pixel_range = moment_request.mutable_pixel_range();
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
    auto moment_generator =
        std::make_unique<carta::MomentGenerator>(FITS_FILE_FULL_NAME, image.get(), spectral_axis, stokes_axis, progress_callback);

    std::vector<carta::CollapseResult> results = moment_generator->CalculateMoments(file_id, 0, moment_request, moment_response);

    moment_generator.reset();

    // Print protobuf messages
    std::cout << "==========================================" << std::endl;
    carta::MomentGenerator::Print(moment_request);
    std::cout << "==========================================" << std::endl;
    carta::MomentGenerator::Print(moment_response);

    for (int i = 0; i < results.size(); ++i) {
        carta::CollapseResult& result = results[i];
        std::shared_ptr<casacore::ImageInterface<casacore::Float>> result_image = result.image;

        casacore::Array<casacore::Float> temp_array;
        casacore::IPosition start(result_image->shape().size(), 0);
        casacore::IPosition count(result_image->shape());
        casacore::Slicer slice(start, count);
        result_image->doGetSlice(temp_array, slice);
        std::vector<Float> temp_vector = temp_array.tovector();

        // Copy an result image
        auto new_image = std::make_unique<casacore::PagedImage<casacore::Float>>(
            result_image->shape(), result_image->coordinates(), OUT_CASA_FILE_FULL_NAME);
        new_image->setMiscInfo(result_image->miscInfo());
        new_image->setImageInfo(result_image->imageInfo());
        new_image->appendLog(result_image->logger());
        new_image->setUnits(result_image->units());
        new_image->putSlice(temp_array, start);
    }
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

    carta::FilesManager moment_files_manager("./");
    moment_files_manager.SaveFile(CASA_FILE_FULL_NAME, image.get(), save_file_msg, save_file_ack);

    std::cout << "==========================================" << std::endl;
    carta::FilesManager::Print(save_file_msg);
    std::cout << "==========================================" << std::endl;
    carta::FilesManager::Print(save_file_ack);
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

    carta::FilesManager moment_files_manager("./");
    moment_files_manager.SaveFile(FITS_FILE_FULL_NAME, image.get(), save_file_msg, save_file_ack);

    std::cout << "==========================================" << std::endl;
    carta::FilesManager::Print(save_file_msg);
    std::cout << "==========================================" << std::endl;
    carta::FilesManager::Print(save_file_ack);
}

void TestCalculateMoments() {
    // auto image = std::make_unique<casacore::FITSImage>(FITS_FILE_FULL_NAME);
    auto image = std::make_unique<casacore::PagedImage<float>>(CASA_FILE_FULL_NAME);

    // Get spectral and stokes indices
    casacore::CoordinateSystem coord_sys = image->coordinates();
    casacore::Vector<casacore::Int> linear_axes = coord_sys.linearAxesNumbers();
    int spectral_axis = coord_sys.spectralAxisNumber();
    int stokes_axis = coord_sys.polarizationAxisNumber();

    // Set moment request message
    CARTA::MomentRequest moment_request;
    int file_id = 0;
    moment_request.set_file_id(file_id);
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
    auto* spectral_range = moment_request.mutable_spectral_range();
    spectral_range->set_min(0);
    spectral_range->set_max(249);
    moment_request.set_mask(CARTA::MomentMask::None);
    auto* pixel_range = moment_request.mutable_pixel_range();
    pixel_range->set_min(-1.0);
    pixel_range->set_max(1.0);

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
    auto moment_generator =
        std::make_unique<carta::MomentGenerator>(FITS_FILE_FULL_NAME, image.get(), spectral_axis, stokes_axis, progress_callback);

    auto t_start = std::chrono::high_resolution_clock::now();

    std::vector<carta::CollapseResult> results = moment_generator->CalculateMoments(file_id, 0, moment_request, moment_response);

    auto t_end = std::chrono::high_resolution_clock::now();
    auto dt = std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_start).count();
    std::cout << "Time spend for CalculateMomentsStoppable(): " << dt * 1e-3 << " ms\n";
}

void TestMedianCoord() {
    // auto image = std::make_unique<casacore::FITSImage>(FITS_FILE_FULL_NAME);
    auto image = std::make_unique<casacore::PagedImage<float>>(CASA_FILE_FULL_NAME);

    casacore::IPosition image_shape = image->shape();
    casacore::IPosition nice_shape = image->niceCursorShape();
    for (int i = 0; i < nice_shape.size(); ++i) {
        std::cout << "nice_shape[" << i << "] = " << nice_shape[i] << ", image_shape[" << i << "] = " << image_shape[i] << "\n";
    }

    // Get spectral and stokes indices
    casacore::CoordinateSystem coord_sys = image->coordinates();
    casacore::Vector<casacore::Int> linear_axes = coord_sys.linearAxesNumbers();
    int spectral_axis = coord_sys.spectralAxisNumber();
    int stokes_axis = coord_sys.polarizationAxisNumber();

    // Set moment request message
    CARTA::MomentRequest moment_request;
    int file_id = 0;
    moment_request.set_file_id(file_id);
    moment_request.set_region_id(-1);

    // moment_request.add_moments(CARTA::Moment::MEAN_OF_THE_SPECTRUM);
    // moment_request.add_moments(CARTA::Moment::INTEGRATED_OF_THE_SPECTRUM);
    // moment_request.add_moments(CARTA::Moment::INTENSITY_WEIGHTED_COORD);
    // moment_request.add_moments(CARTA::Moment::INTENSITY_WEIGHTED_DISPERSION_OF_THE_COORD);
    // moment_request.add_moments(CARTA::Moment::MEDIAN_OF_THE_SPECTRUM);
    // moment_request.add_moments(CARTA::Moment::STD_ABOUT_THE_MEAN_OF_THE_SPECTRUM);
    // moment_request.add_moments(CARTA::Moment::RMS_OF_THE_SPECTRUM);
    // moment_request.add_moments(CARTA::Moment::ABS_MEAN_DEVIATION_OF_THE_SPECTRUM);
    // moment_request.add_moments(CARTA::Moment::MAX_OF_THE_SPECTRUM);
    // moment_request.add_moments(CARTA::Moment::COORD_OF_THE_MAX_OF_THE_SPECTRUM);
    // moment_request.add_moments(CARTA::Moment::MIN_OF_THE_SPECTRUM);
    // moment_request.add_moments(CARTA::Moment::COORD_OF_THE_MIN_OF_THE_SPECTRUM);

    moment_request.add_moments(CARTA::Moment::MEDIAN_COORDINATE);

    moment_request.set_axis(CARTA::MomentAxis::SPECTRAL);
    auto* spectral_range = moment_request.mutable_spectral_range();
    spectral_range->set_min(0);
    spectral_range->set_max(249);
    moment_request.set_mask(CARTA::MomentMask::None);
    auto* pixel_range = moment_request.mutable_pixel_range();
    pixel_range->set_min(0.0);
    pixel_range->set_max(1.0);

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
    auto moment_generator =
        std::make_unique<carta::MomentGenerator>(FITS_FILE_FULL_NAME, image.get(), spectral_axis, stokes_axis, progress_callback);

    auto t_start = std::chrono::high_resolution_clock::now();

    std::vector<carta::CollapseResult> results = moment_generator->CalculateMoments(file_id, 0, moment_request, moment_response);

    auto t_end = std::chrono::high_resolution_clock::now();
    auto dt = std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_start).count();
    std::cout << "Time spend for CalculateMomentsStoppable(): " << dt * 1e-3 << " ms\n";
}
