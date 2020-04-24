#include "MomentGenerator.h"

using namespace carta;

MomentGenerator::MomentGenerator(
    casacore::ImageInterface<float>* image, int spectral_axis, int stokes_axis, const CARTA::MomentRequest& moment_request)
    : _collapse_error(false), _base_name("") {
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

    if (region.nfields() == 0) {
        String empty("");
        std::shared_ptr<const ImageInterface<float>> image_shared_ptr(image);
        if (image_shared_ptr) {
            // Make a sub-image
            std::shared_ptr<const SubImage<Float>> sub_image_shared_ptr =
                casa::SubImageFactory<Float>::createSubImageRO(*image, region, empty, NULL);
            ImageInterface<Float>* sub_image = new SubImage<Float>(*sub_image_shared_ptr); // Does it need to be deleted later?
            LogOrigin log("MomentController", "SetMomentGenerator", WHERE);
            LogIO os(log);
            // Make an ImageMoments object
            _image_moments = new casa::ImageMoments<Float>(*sub_image, os);
            // Calculate the moments
            ExecuteMomentGenerator();
        }
    }
}

MomentGenerator::~MomentGenerator() {
    delete _image_moments;
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
        // casa::utilj::ThreadTimes t1;
        String out_file;
        bool output_file_temporary = GetOutputFileName(out_file, 0, _channels);
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
                        CollapseResult collapse_result(out_file, output_file_temporary, result_image);
                        _collapse_results.push_back(collapse_result);
                    }
                } catch (const AipsError& x) {
                    _error_msg = x.getMesg();
                    _collapse_error = true;
                }
            }
        }
        // casa::utilj::ThreadTimes t2;
        // casa::utilj::DeltaThreadTimes dt = t2 - t1;
        // qDebug() << "Elapsed time moment="<<moments[0]<< "
        // elapsed="<<dt.elapsed()<<" cpu="<<dt.cpu();
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
    _channels = std::to_string(chan_min) + "-" + std::to_string(chan_max);       // For the output file name
    uInt num_selected_channels = chan_max - chan_min + 1;

    // Make a region record
    std::shared_ptr<const ImageInterface<float>> image_shared_ptr(image);
    CoordinateSystem coordinate_system = image_shared_ptr->coordinates();
    IPosition pos = image_shared_ptr->shape();
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
            mode = -1;
            break;
        }
        case CARTA::Moment::INTEGRATED_OF_THE_SPECTRUM: {
            mode = 0;
            break;
        }
        case CARTA::Moment::INTENSITY_WEIGHTED_COORD: {
            mode = 1;
            break;
        }
        case CARTA::Moment::INTENSITY_WEIGHTED_DISPERSION_OF_THE_COORD: {
            mode = 2;
            break;
        }
        case CARTA::Moment::MEDIAN_OF_THE_SPECTRUM: {
            mode = 3;
            break;
        }
        case CARTA::Moment::MEDIAN_COORDINATE: {
            mode = 4;
            break;
        }
        case CARTA::Moment::STD_DEV_ABOUT_THE_MEAN_OF_THE_SPECTRUM: {
            mode = 5;
            break;
        }
        case CARTA::Moment::RMS_OF_THE_SPECTRUM: {
            mode = 6;
            break;
        }
        case CARTA::Moment::ABS_MEAN_DEVIATION_OF_THE_SPECTRUM: {
            mode = 7;
            break;
        }
        case CARTA::Moment::MAX_OF_THE_SPECTRUM: {
            mode = 8;
            break;
        }
        case CARTA::Moment::COORD_OF_THE_MAX_OF_THE_SPECTRUM: {
            mode = 9;
            break;
        }
        case CARTA::Moment::MIN_OF_THE_SPECTRUM: {
            mode = 10;
            break;
        }
        case CARTA::Moment::COORD_OF_THE_MIN_OF_THE_SPECTRUM: {
            mode = 11;
            break;
        }
    }
    return mode;
}

bool MomentGenerator::GetOutputFileName(casacore::String& out_name, int moment, const casacore::String& channel) const {
    bool success = true;
    if (_output_filename.empty()) { // Use a default base name
        out_name = _base_name;
    } else { // Use the user specified name
        out_name = _output_filename;
        success = false;
    }

    // Prepend the channel and moment used to make it descriptive.
    // outName = outName + "_" + String(momentNames[moment].toStdString());
    if (channel != "") {
        out_name = out_name + "_" + channel;
    }

    return success;
}

bool MomentGenerator::IsSuccess() const {
    bool success = false;
    if (!_collapse_results.empty() && !_collapse_error) {
        success = true;
    }
    return success;
}

void MomentGenerator::SetOutputFileName(std::string output_filename) {
    _output_filename = output_filename;
}

std::vector<CollapseResult> MomentGenerator::GetResults() const {
    return _collapse_results;
}

casacore::String MomentGenerator::GetErrorMessage() const {
    return _error_msg;
}