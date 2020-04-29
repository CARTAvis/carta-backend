#include "MomentGenerator.h"

using namespace carta;

MomentGenerator::MomentGenerator(
    casacore::ImageInterface<float>* image, int spectral_axis, int stokes_axis, const CARTA::MomentRequest& moment_request)
    : _image_moments(nullptr), _collapse_error(false) {
    // Set moment axis
    if (moment_request.axis() == CARTA::MomentAxis::SPECTRAL) {
        _axis = spectral_axis;
    } else if (moment_request.axis() == CARTA::MomentAxis::STOKES) {
        _axis = stokes_axis;
    } else {
        std::cerr << "Do nto support the moment axis: " << moment_request.axis() << std::endl;
    }

    // Set moment types vector
    int moments_size(moment_request.moments_size());
    _moments.resize(moments_size);
    for (int i = 0; i < moments_size; ++i) {
        _moments[i] = GetMomentMode(moment_request.moments(i));
    }

    // Set pixel range
    SetPixelRange(moment_request);

    // Make a region record
    Record region = MakeRegionRecord(image, moment_request);

    // Make a sub image interface
    String empty("");
    std::shared_ptr<const SubImage<Float>> sub_image_shared_ptr =
        casa::SubImageFactory<Float>::createSubImageRO(*image, region, empty, NULL);
    ImageInterface<Float>* sub_image = new SubImage<Float>(*sub_image_shared_ptr); // Does it need to be deleted later?
    LogOrigin log("MomentController", "SetMomentGenerator", WHERE);
    LogIO os(log);

    // Make an ImageMoments object
    _image_moments = new casa::ImageMoments<casacore::Float>(casacore::SubImage<casacore::Float>(*sub_image), os);

    // Calculate the moment images
    ExecuteMomentGenerator();
}

MomentGenerator::~MomentGenerator() {
    if (_image_moments) {
        delete _image_moments;
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
    } else if (moment_mask == CARTA::MomentMask::Exclude) {
        if ((pixel_min == ALL_PIXEL_RANGE) || (pixel_max == ALL_PIXEL_RANGE)) {
            _exclude_pix.resize(1);
            _exclude_pix[0] = ALL_PIXEL_RANGE;
        } else {
            _exclude_pix.resize(2);
            _exclude_pix[0] = pixel_min;
            _exclude_pix[1] = pixel_max;
        }
    } else {
        _include_pix.resize(1);
        _include_pix[0] = -1;
    }
}

void MomentGenerator::ExecuteMomentGenerator() {
    try {
        String out_file;
        if (!_image_moments->setMoments(_moments)) {
            _error_msg = _image_moments->errorMessage();
            _collapse_error = true;
        } else {
            if (!_image_moments->setMomentAxis(_axis)) {
                _error_msg = _image_moments->errorMessage();
                _collapse_error = true;
            } else {
                try {
                    _image_moments->setInExCludeRange(_include_pix, _exclude_pix);
                    auto result_images = _image_moments->createMoments(false, out_file, false);
                    for (int i = 0; i < result_images.size(); ++i) {
                        std::shared_ptr<ImageInterface<Float>> result_image = dynamic_pointer_cast<ImageInterface<Float>>(result_images[i]);
                        CollapseResult collapse_result(_moments[i], result_image, out_file);
                        _collapse_results.push_back(collapse_result);
                    }
                } catch (const AipsError& x) {
                    _error_msg = x.getMesg();
                    _collapse_error = true;
                }
            }
        }
    } catch (AipsError& error) {
        _error_msg = error.getLastMessage();
        _collapse_error = true;
    }
}

Record MomentGenerator::MakeRegionRecord(casacore::ImageInterface<float>* image, const CARTA::MomentRequest& moment_request) {
    // QString fileName = taskMonitor->getImagePath();
    // String infile(fileName.toStdString());
    String infile = ""; // Original purpose is to access the region record from the image file

    // Initialize the channels
    int chan_min(moment_request.spectral_range().min());
    int chan_max(moment_request.spectral_range().max());

    // Check if the channel range is logical
    if (chan_max < chan_min) {
        int tmp = chan_max;
        chan_max = chan_min;
        chan_min = tmp;
    }

    String channels = std::to_string(chan_min) + "~" + std::to_string(chan_max); // Channel range for the moments calculation
    uInt num_selected_channels = chan_max - chan_min + 1;

    // Set the stokes (not apply this variable yet!)
    String tmp_stokes = GetStokes(moment_request.stokes()); // "I" , "IV" , "IQU", or "IQUV"

    // Make a region record
    CoordinateSystem coordinate_system = image->coordinates();
    IPosition pos = image->shape();
    image->shape();
    String region_name;
    String stokes = ""; // Not available yet, in principle can choose "I" , "IV" , "IQU", or "IQUV"
    casa::CasacRegionManager crm(coordinate_system);
    String diagnostics;
    String pixel_box = ""; // Not available yet
    Record region = crm.fromBCS(diagnostics, num_selected_channels, stokes, NULL, region_name, channels,
        casa::CasacRegionManager::USE_FIRST_STOKES, pixel_box, pos, infile);

    return region;
}

int MomentGenerator::GetMomentMode(CARTA::Moment moment) {
    int mode(-1);
    switch (moment) {
        case CARTA::Moment::MEAN_OF_THE_SPECTRUM: {
            mode = casa::ImageMoments<casacore::Float>::AVERAGE;
            break;
        }
        case CARTA::Moment::INTEGRATED_OF_THE_SPECTRUM: {
            mode = casa::ImageMoments<casacore::Float>::INTEGRATED;
            break;
        }
        case CARTA::Moment::INTENSITY_WEIGHTED_COORD: {
            mode = casa::ImageMoments<casacore::Float>::WEIGHTED_MEAN_COORDINATE;
            break;
        }
        case CARTA::Moment::INTENSITY_WEIGHTED_DISPERSION_OF_THE_COORD: {
            mode = casa::ImageMoments<casacore::Float>::WEIGHTED_DISPERSION_COORDINATE;
            break;
        }
        case CARTA::Moment::MEDIAN_OF_THE_SPECTRUM: {
            mode = casa::ImageMoments<casacore::Float>::MEDIAN;
            break;
        }
        case CARTA::Moment::MEDIAN_COORDINATE: {
            mode = casa::ImageMoments<casacore::Float>::MEDIAN_COORDINATE;
            break;
        }
        case CARTA::Moment::STD_DEV_ABOUT_THE_MEAN_OF_THE_SPECTRUM: {
            mode = casa::ImageMoments<casacore::Float>::STANDARD_DEVIATION;
            break;
        }
        case CARTA::Moment::RMS_OF_THE_SPECTRUM: {
            mode = casa::ImageMoments<casacore::Float>::RMS;
            break;
        }
        case CARTA::Moment::ABS_MEAN_DEVIATION_OF_THE_SPECTRUM: {
            mode = casa::ImageMoments<casacore::Float>::ABS_MEAN_DEVIATION;
            break;
        }
        case CARTA::Moment::MAX_OF_THE_SPECTRUM: {
            mode = casa::ImageMoments<casacore::Float>::MAXIMUM;
            break;
        }
        case CARTA::Moment::COORD_OF_THE_MAX_OF_THE_SPECTRUM: {
            mode = casa::ImageMoments<casacore::Float>::MAXIMUM_COORDINATE;
            break;
        }
        case CARTA::Moment::MIN_OF_THE_SPECTRUM: {
            mode = casa::ImageMoments<casacore::Float>::MINIMUM;
            break;
        }
        case CARTA::Moment::COORD_OF_THE_MIN_OF_THE_SPECTRUM: {
            mode = casa::ImageMoments<casacore::Float>::MINIMUM_COORDINATE;
            break;
        }
        default: {
            std::cerr << "Unknown moment mode: " << moment << std::endl;
            break;
        }
    }
    return mode;
}

String MomentGenerator::GetStokes(CARTA::MomentStokes moment_stokes) {
    String stokes("");
    switch (moment_stokes) {
        case CARTA::MomentStokes::I: {
            stokes = "I";
            break;
        }
        case CARTA::MomentStokes::IV: {
            stokes = "IV";
            break;
        }
        case CARTA::MomentStokes::IQU: {
            stokes = "IQU";
            break;
        }
        case CARTA::MomentStokes::IQUV: {
            stokes = "IQUV";
            break;
        }
        default: {
            std::cout << "Unknown stokes!" << std::endl;
            break;
        }
    }
    return stokes;
}

bool MomentGenerator::IsSuccess() const {
    bool success = false;
    if (!_collapse_results.empty() && !_collapse_error) {
        success = true;
    }
    return success;
}

std::vector<CollapseResult> MomentGenerator::GetResults() const {
    return _collapse_results;
}

casacore::String MomentGenerator::GetErrorMessage() const {
    return _error_msg;
}