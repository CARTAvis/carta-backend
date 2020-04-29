#include <iostream>
#include <limits>

#include "../Moment/MomentController.h"
#include "../Moment/MomentGenerator.h"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <image_file>" << std::endl;
        return 1;
    }

    std::string filename = argv[1];

    // Open an CASA image file
    casacore::PagedImage<casacore::Float> in_image(filename);

    // Construct moment helper object
    casacore::LogOrigin log_origin("myClass", "myFunction(...)", WHERE);
    casacore::LogIO os(log_origin);
    casa::ImageMoments<casacore::Float> moment(casacore::SubImage<casacore::Float>(in_image), os);

    // Set state function argument values
    casacore::Vector<casacore::Int> moments(2);
    moments(0) = casa::ImageMoments<casacore::Float>::AVERAGE;
    moments(1) = casa::ImageMoments<casacore::Float>::WEIGHTED_MEAN_COORDINATE;
    // casacore::Vector<int> methods(2);
    // methods(0) = casa::ImageMoments<casacore::Float>::WINDOW;
    // methods(1) = casa::ImageMoments<casacore::Float>::FIT;

    // Specify state via control functions
    if (!moment.setMoments(moments)) {
        return 1;
    }
    // if (!moment.setWinFitMethod(methods)) {
    //    return 1;
    //}
    if (!moment.setMomentAxis(3)) {
        return 1;
    }

    // Print the original image file info
    std::cout << "file name: " << filename << std::endl;
    std::cout << "in_image.shape().size(): " << in_image.shape().size() << std::endl;
    std::cout << "in_image.shape().nelements(): " << in_image.shape().nelements() << std::endl;
    for (int i = 0; i < in_image.shape().size(); ++i) {
        std::cout << "in_image.shape()[" << i << "]= " << in_image.shape()[i] << std::endl;
    }

    // Create the moments
    String out_file;
    auto result_images = moment.createMoments(false, out_file, false);
    std::cout << "result_images.size(): " << result_images.size() << std::endl;

    for (int i = 0; i < result_images.size(); ++i) {
        std::cout << "result_image[" << i << "]:" << std::endl;
        std::shared_ptr<ImageInterface<Float>> result_image = dynamic_pointer_cast<ImageInterface<Float>>(result_images[i]);
        std::cout << "result_image->shape().size(): " << result_image->shape().size() << std::endl;
        std::cout << "result_image->shape().nelements(): " << result_image->shape().nelements() << std::endl;
        for (int j = 0; j < result_image->shape().size(); ++j) {
            std::cout << "result_image->shape()[" << j << "]= " << result_image->shape()[j] << std::endl;
        }
    }

    return 0;
}
