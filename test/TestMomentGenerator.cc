#include <iostream>
#include <limits>

#include "../Moment/MomentController.h"
#include "../Moment/MomentGenerator.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <image_file>" << std::endl;
        return 1;
    }

    // Set the image file name
    std::string filename = argv[1];

    // Open an CASA image file
    std::shared_ptr<casacore::PagedImage<float>> image;
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
    moment_request.add_moments(CARTA::Moment::INTENSITY_WEIGHTED_COORD);
    moment_request.set_axis(CARTA::MomentAxis::SPECTRAL);
    auto spectral_range = moment_request.mutable_spectral_range();
    spectral_range->set_min(1);
    spectral_range->set_max(10);
    moment_request.set_mask(CARTA::MomentMask::None);
    auto pixel_range = moment_request.mutable_pixel_range();
    pixel_range->set_min(0.0);
    pixel_range->set_max(100.0);

    // Create a moment generator object
    auto moment_generator =
        std::unique_ptr<carta::MomentGenerator>(new carta::MomentGenerator(image.get(), spectral_axis, stokes_axis, moment_request));

    std::vector<carta::CollapseResult> moment_results = moment_generator->GetResults();
    std::cout << "moment_results.size(): " << moment_results.size() << std::endl;

    for (int i = 0; i < moment_results.size(); ++i) {
        std::cout << "moment_type: " << moment_results[i].moment_type << std::endl;
        std::shared_ptr<ImageInterface<Float>> result_image = dynamic_pointer_cast<ImageInterface<Float>>(moment_results[i].image);
        std::cout << "result_image->shape().size(): " << result_image->shape().size() << std::endl;
        std::cout << "result_image->shape().nelements(): " << result_image->shape().nelements() << std::endl;
        for (int j = 0; j < result_image->shape().size(); ++j) {
            std::cout << "result_image->shape()[" << j << "]= " << result_image->shape()[j] << std::endl;
        }
    }

    return 0;
}
