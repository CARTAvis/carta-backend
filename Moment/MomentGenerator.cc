#include "MomentGenerator.h"

using namespace carta;

MomentGenerator::MomentGenerator(const casacore::String& filename, casacore::ImageInterface<float>* image, int spectral_axis,
    int stokes_axis, MomentProgressCallback progress_callback)
    : _filename(filename),
      _image(image),
      _spectral_axis(spectral_axis),
      _stokes_axis(stokes_axis),
      _sub_image(nullptr),
      _image_moments(nullptr),
      _collapse_error(false),
      _progress_callback(progress_callback) {}

std::vector<CollapseResult> MomentGenerator::CalculateMoments(int file_id, const casacore::ImageRegion& image_region,
    const CARTA::MomentRequest& moment_request, CARTA::MomentResponse& moment_response) {
    // Collapse results
    std::vector<CollapseResult> collapse_results;

    // Set moment axis
    SetMomentAxis(moment_request);

    // Set moment types
    SetMomentTypes(moment_request);

    // Set pixel range
    SetPixelRange(moment_request);

    // Reset an ImageMoments
    ResetImageMoments(image_region);

    // Calculate moments
    try {
        if (!_image_moments->setMoments(_moments)) {
            _error_msg = _image_moments->errorMessage();
            _collapse_error = true;
        } else {
            if (!_image_moments->setMomentAxis(_axis)) {
                _error_msg = _image_moments->errorMessage();
                _collapse_error = true;
            } else {
                casacore::Bool do_temp = true;
                casacore::Bool remove_axis = false;
                casacore::String out_file = GetOutputFileName();
                std::size_t found = out_file.find_last_of("/");
                std::string file_base_name = out_file.substr(found + 1);
                try {
                    _image_moments->setInExCludeRange(_include_pix, _exclude_pix);

                    // Save collapse results in the memory
                    auto result_images = _image_moments->createMoments(do_temp, out_file, remove_axis);

                    for (int i = 0; i < result_images.size(); ++i) {
                        // Set temp moment file name
                        std::string moment_suffix = GetMomentSuffix(_moments[i]);
                        std::string out_file_name = file_base_name + "." + moment_suffix;

                        // Set temp moment file id
                        int moment_type = _moments[i];
                        int moment_file_id = (file_id + 1) * OUTPUT_ID_MULTIPLIER + moment_type;

                        // Fill results
                        std::shared_ptr<casacore::ImageInterface<casacore::Float>> moment_image =
                            dynamic_pointer_cast<casacore::ImageInterface<casacore::Float>>(result_images[i]);
                        collapse_results.push_back(CollapseResult(moment_file_id, out_file_name, moment_image));
                    }
                } catch (const AipsError& x) {
                    _error_msg = x.getMesg();
                    _collapse_error = true;
                    std::cerr << "Error: " << _error_msg << "\n";
                }
            }
        }
    } catch (AipsError& error) {
        _error_msg = error.getLastMessage();
        _collapse_error = true;
    }

    // Set is the moment calculation successful or not
    moment_response.set_success(IsSuccess());

    // Get error message if any
    moment_response.set_message(GetErrorMessage());

    return collapse_results;
}

void MomentGenerator::StopCalculation() {
    if (_image_moments) {
        _image_moments->StopCalculation();
    }
}

void MomentGenerator::SetMomentAxis(const CARTA::MomentRequest& moment_request) {
    if (moment_request.axis() == CARTA::MomentAxis::SPECTRAL) {
        _axis = _spectral_axis;
    } else if (moment_request.axis() == CARTA::MomentAxis::STOKES) {
        _axis = _stokes_axis;
    } else {
        std::cerr << "Do not support the moment axis: " << moment_request.axis() << std::endl;
    }
}

void MomentGenerator::SetMomentTypes(const CARTA::MomentRequest& moment_request) {
    int moments_size(moment_request.moments_size());
    _moments.resize(moments_size);
    for (int i = 0; i < moments_size; ++i) {
        _moments[i] = GetMomentMode(moment_request.moments(i));
    }
}

void MomentGenerator::SetPixelRange(const CARTA::MomentRequest& moment_request) {
    // Set include or exclude pixel range
    CARTA::MomentMask moment_mask = moment_request.mask();
    float pixel_min(moment_request.pixel_range().min());
    float pixel_max(moment_request.pixel_range().max());
    if (pixel_max < pixel_min) {
        float tmp = pixel_max;
        pixel_max = pixel_min;
        pixel_min = tmp;
    }
    if (moment_mask == CARTA::MomentMask::Include) {
        if ((pixel_min == ALL_PIXEL_RANGE) || (pixel_max == ALL_PIXEL_RANGE)) {
            _include_pix.resize(1);
            _include_pix[0] = ALL_PIXEL_RANGE;
        } else {
            _include_pix.resize(2);
            _include_pix[0] = pixel_min;
            _include_pix[1] = pixel_max;
        }
        // Clear the exclusive array
        _exclude_pix.resize(0);
    } else if (moment_mask == CARTA::MomentMask::Exclude) {
        if ((pixel_min == ALL_PIXEL_RANGE) || (pixel_max == ALL_PIXEL_RANGE)) {
            _exclude_pix.resize(1);
            _exclude_pix[0] = ALL_PIXEL_RANGE;
        } else {
            _exclude_pix.resize(2);
            _exclude_pix[0] = pixel_min;
            _exclude_pix[1] = pixel_max;
        }
        // Clear the inclusive array
        _include_pix.resize(0);
    } else {
        _include_pix.resize(2);
        _include_pix[0] = std::numeric_limits<float>::min();
        _include_pix[1] = std::numeric_limits<float>::max();
        // Clear the exclusive array
        _exclude_pix.resize(0);
    }
}

void MomentGenerator::ResetImageMoments(const casacore::ImageRegion& image_region) {
    // Reset the sub-image
    _sub_image.reset(new casacore::SubImage<casacore::Float>(*_image, image_region));

    casacore::LogOrigin log("MomentGenerator", "MomentGenerator", WHERE);
    casacore::LogIO os(log);

    // Make an ImageMoments object (and overwrite the output file if it already exists)
    _image_moments.reset(new ImageMoments<casacore::Float>(casacore::SubImage<casacore::Float>(*_sub_image), os, true));

    // Set moment calculation progress monitor
    _image_moments->setProgressMonitor(this);
}

int MomentGenerator::GetMomentMode(CARTA::Moment moment) {
    int mode(-1);
    switch (moment) {
        case CARTA::Moment::MEAN_OF_THE_SPECTRUM: {
            mode = ImageMoments<casacore::Float>::AVERAGE;
            break;
        }
        case CARTA::Moment::INTEGRATED_OF_THE_SPECTRUM: {
            mode = ImageMoments<casacore::Float>::INTEGRATED;
            break;
        }
        case CARTA::Moment::INTENSITY_WEIGHTED_COORD: {
            mode = ImageMoments<casacore::Float>::WEIGHTED_MEAN_COORDINATE;
            break;
        }
        case CARTA::Moment::INTENSITY_WEIGHTED_DISPERSION_OF_THE_COORD: {
            mode = ImageMoments<casacore::Float>::WEIGHTED_DISPERSION_COORDINATE;
            break;
        }
        case CARTA::Moment::MEDIAN_OF_THE_SPECTRUM: {
            mode = ImageMoments<casacore::Float>::MEDIAN;
            break;
        }
        case CARTA::Moment::MEDIAN_COORDINATE: {
            mode = ImageMoments<casacore::Float>::MEDIAN_COORDINATE;
            break;
        }
        case CARTA::Moment::STD_ABOUT_THE_MEAN_OF_THE_SPECTRUM: {
            mode = ImageMoments<casacore::Float>::STANDARD_DEVIATION;
            break;
        }
        case CARTA::Moment::RMS_OF_THE_SPECTRUM: {
            mode = ImageMoments<casacore::Float>::RMS;
            break;
        }
        case CARTA::Moment::ABS_MEAN_DEVIATION_OF_THE_SPECTRUM: {
            mode = ImageMoments<casacore::Float>::ABS_MEAN_DEVIATION;
            break;
        }
        case CARTA::Moment::MAX_OF_THE_SPECTRUM: {
            mode = ImageMoments<casacore::Float>::MAXIMUM;
            break;
        }
        case CARTA::Moment::COORD_OF_THE_MAX_OF_THE_SPECTRUM: {
            mode = ImageMoments<casacore::Float>::MAXIMUM_COORDINATE;
            break;
        }
        case CARTA::Moment::MIN_OF_THE_SPECTRUM: {
            mode = ImageMoments<casacore::Float>::MINIMUM;
            break;
        }
        case CARTA::Moment::COORD_OF_THE_MIN_OF_THE_SPECTRUM: {
            mode = ImageMoments<casacore::Float>::MINIMUM_COORDINATE;
            break;
        }
        default: {
            std::cerr << "Unknown moment mode: " << moment << std::endl;
            break;
        }
    }
    return mode;
}

casacore::String MomentGenerator::GetMomentSuffix(casacore::Int moment) {
    casacore::String suffix;
    switch (moment) {
        case ImageMoments<casacore::Float>::AVERAGE: {
            suffix = "average";
            break;
        }
        case ImageMoments<casacore::Float>::INTEGRATED: {
            suffix = "integrated";
            break;
        }
        case ImageMoments<casacore::Float>::WEIGHTED_MEAN_COORDINATE: {
            suffix = "weighted_coord";
            break;
        }
        case ImageMoments<casacore::Float>::WEIGHTED_DISPERSION_COORDINATE: {
            suffix = "weighted_dispersion_coord";
            break;
        }
        case ImageMoments<casacore::Float>::MEDIAN: {
            suffix = "median";
            break;
        }
        case ImageMoments<casacore::Float>::MEDIAN_COORDINATE: {
            suffix = "median_coord";
            break;
        }
        case ImageMoments<casacore::Float>::STANDARD_DEVIATION: {
            suffix = "standard_deviation";
            break;
        }
        case ImageMoments<casacore::Float>::RMS: {
            suffix = "rms";
            break;
        }
        case ImageMoments<casacore::Float>::ABS_MEAN_DEVIATION: {
            suffix = "abs_mean_dev";
            break;
        }
        case ImageMoments<casacore::Float>::MAXIMUM: {
            suffix = "maximum";
            break;
        }
        case ImageMoments<casacore::Float>::MAXIMUM_COORDINATE: {
            suffix = "maximum_coord";
            break;
        }
        case ImageMoments<casacore::Float>::MINIMUM: {
            suffix = "minimum";
            break;
        }
        case ImageMoments<casacore::Float>::MINIMUM_COORDINATE: {
            suffix = "minimum_coord";
            break;
        }
        default: {
            std::cerr << "Unknown moment mode: " << moment << std::endl;
            break;
        }
    }
    return suffix;
}

casacore::String MomentGenerator::GetOutputFileName() {
    // Store moment images in a temporary folder
    casacore::String result(_filename);
    result += ".moment";
    size_t found = result.find_last_of("/");
    if (found) {
        result.replace(0, found, "");
    }
    return result;
}

bool MomentGenerator::IsSuccess() const {
    bool success = false;
    if (!_collapse_error) {
        success = true;
    }
    return success;
}

casacore::String MomentGenerator::GetErrorMessage() const {
    return _error_msg;
}

void MomentGenerator::setStepCount(int count) {
    // Initialize the progress parameters
    _total_steps = count;
    _progress = 0.0;
    _pre_progress = 0.0;
}

void MomentGenerator::setStepsCompleted(int count) {
    _progress = (float)count / _total_steps;
    // Update the progress report every percent
    if ((_progress - _pre_progress) >= REPORT_PROGRESS_EVERY_FACTOR) {
        if (_progress > MOMENT_COMPLETE) {
            _progress = MOMENT_COMPLETE;
        }
        // Report the progress
        _progress_callback(_progress);
        _pre_progress = _progress;
    }
}

void MomentGenerator::done() {}
