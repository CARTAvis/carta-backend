#include "MomentGenerator.h"

using namespace carta;

MomentGenerator::MomentGenerator(const String& filename, casacore::ImageInterface<float>* image, int spectral_axis, int stokes_axis,
    const CARTA::MomentRequest& moment_request, CARTA::MomentResponse& moment_response, MomentProgressCallback progress_callback)
    : _filename(filename), _image_moments(nullptr), _collapse_error(false), _progress_callback(progress_callback) {
    // Set moment axis
    if (moment_request.axis() == CARTA::MomentAxis::SPECTRAL) {
        _axis = spectral_axis;
    } else if (moment_request.axis() == CARTA::MomentAxis::STOKES) {
        _axis = stokes_axis;
    } else {
        std::cerr << "Do not support the moment axis: " << moment_request.axis() << std::endl;
    }

    // Set moment types
    SetMomentTypes(moment_request);

    // Set pixel range
    SetPixelRange(moment_request);

    // Make a region record
    Record region = MakeRegionRecord(image, moment_request);

    // Make a sub image interface
    String empty("");
    std::shared_ptr<const SubImage<Float>> sub_image_shared_ptr =
        casa::SubImageFactory<Float>::createSubImageRO(*image, region, empty, NULL);
    ImageInterface<Float>* sub_image = new SubImage<Float>(*sub_image_shared_ptr);
    LogOrigin log("MomentGenerator", "MomentGenerator", WHERE);
    LogIO os(log);

    // Make an ImageMoments object (and overwrite the output file if it already exists)
    _image_moments = new casa::ImageMoments<casacore::Float>(casacore::SubImage<casacore::Float>(*sub_image), os, true);

    // Set moment calculation progress monitor
    _image_moments->setProgressMonitor(this);

    // Calculate the moment images
    ExecuteMomentGenerator(moment_request, moment_response);

    // Set is the moment calculation successful or not
    moment_response.set_success(IsSuccess());

    // Get error message if any
    moment_response.set_message(GetErrorMessage());

    // Delete the sub image object
    delete sub_image;
}

MomentGenerator::~MomentGenerator() {
    delete _image_moments;
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

void MomentGenerator::ExecuteMomentGenerator(const CARTA::MomentRequest& moment_request, CARTA::MomentResponse& moment_response) {
    try {
        if (!_image_moments->setMoments(_moments)) {
            _error_msg = _image_moments->errorMessage();
            _collapse_error = true;
        } else {
            if (!_image_moments->setMomentAxis(_axis)) {
                _error_msg = _image_moments->errorMessage();
                _collapse_error = true;
            } else {
                String out_file = GetOutputFileName();
                std::size_t found = out_file.find_last_of("/");
                std::string file_base_name = out_file.substr(found + 1);
                std::string path_name = out_file.substr(0, found);
                moment_response.set_directory(path_name);
                try {
                    _image_moments->setInExCludeRange(_include_pix, _exclude_pix);
                    auto result_images = _image_moments->createMoments(false, out_file, false);
                    for (int i = 0; i < result_images.size(); ++i) {
                        std::shared_ptr<ImageInterface<Float>> result_image = dynamic_pointer_cast<ImageInterface<Float>>(result_images[i]);
                        std::string moment_suffix = GetMomentSuffix(_moments[i]);
                        std::string output_filename;
                        if (result_images.size() == 1) {
                            output_filename = file_base_name;
                        } else {
                            output_filename = file_base_name + "." + moment_suffix;
                        }
                        auto* output_files = moment_response.add_output_files();
                        output_files->set_file_name(output_filename);
                        output_files->set_moment_type(moment_request.moments(i));
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
    _channels = std::to_string(chan_min) + "_" + std::to_string(chan_max);       // This is used to set the output file name
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
        case CARTA::Moment::STD_ABOUT_THE_MEAN_OF_THE_SPECTRUM: {
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

String MomentGenerator::GetMomentSuffix(casacore::Int moment) {
    String suffix;
    switch (moment) {
        case casa::ImageMoments<casacore::Float>::AVERAGE: {
            suffix = "average";
            break;
        }
        case casa::ImageMoments<casacore::Float>::INTEGRATED: {
            suffix = "integrated";
            break;
        }
        case casa::ImageMoments<casacore::Float>::WEIGHTED_MEAN_COORDINATE: {
            suffix = "weighted_coord";
            break;
        }
        case casa::ImageMoments<casacore::Float>::WEIGHTED_DISPERSION_COORDINATE: {
            suffix = "weighted_dispersion_coord";
            break;
        }
        case casa::ImageMoments<casacore::Float>::MEDIAN: {
            suffix = "median";
            break;
        }
        case casa::ImageMoments<casacore::Float>::MEDIAN_COORDINATE: {
            suffix = "median_coord";
            break;
        }
        case casa::ImageMoments<casacore::Float>::STANDARD_DEVIATION: {
            suffix = "standard_deviation";
            break;
        }
        case casa::ImageMoments<casacore::Float>::RMS: {
            suffix = "rms";
            break;
        }
        case casa::ImageMoments<casacore::Float>::ABS_MEAN_DEVIATION: {
            suffix = "abs_mean_dev";
            break;
        }
        case casa::ImageMoments<casacore::Float>::MAXIMUM: {
            suffix = "maximum";
            break;
        }
        case casa::ImageMoments<casacore::Float>::MAXIMUM_COORDINATE: {
            suffix = "maximum_coord";
            break;
        }
        case casa::ImageMoments<casacore::Float>::MINIMUM: {
            suffix = "minimum";
            break;
        }
        case casa::ImageMoments<casacore::Float>::MINIMUM_COORDINATE: {
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
            std::cerr << "Unknown stokes!" << std::endl;
            break;
        }
    }
    return stokes;
}

String MomentGenerator::GetOutputFileName() {
    String result(_filename);
    if (!_channels.empty()) {
        result += ".ch_" + _channels;
    } else {
        result += ".ch_all";
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

// Print protobuf messages
void MomentGenerator::Print(CARTA::MomentRequest message) {
    std::cout << "CARTA::MomentRequest:" << std::endl;
    std::cout << "file_id = " << message.file_id() << std::endl;
    for (int i = 0; i < message.moments_size(); ++i) {
        Print(message.moments(i));
    }
    Print(message.axis());
    std::cout << "region_id = " << message.region_id() << std::endl;
    std::cout << "spectral_range:" << std::endl;
    Print(message.spectral_range());
    Print(message.stokes());
    Print(message.mask());
    std::cout << "pixel_range:" << std::endl;
    Print(message.pixel_range());
}

void MomentGenerator::Print(CARTA::MomentResponse message) {
    std::cout << "CARTA::MomentResponse:" << std::endl;
    if (message.success()) {
        std::cout << "success = true" << std::endl;
    } else {
        std::cout << "success = false" << std::endl;
    }
    std::cout << "directory = " << message.directory() << std::endl;
    for (int i = 0; i < message.output_files_size(); ++i) {
        Print(message.output_files(i));
    }
}

void MomentGenerator::Print(CARTA::IntBounds message) {
    std::cout << "CARTA::IntBounds:" << std::endl;
    std::cout << "Int min = " << message.min() << std::endl;
    std::cout << "Int max = " << message.max() << std::endl;
}

void MomentGenerator::Print(CARTA::FloatBounds message) {
    std::cout << "CARTA::FloatBounds:" << std::endl;
    std::cout << "Float min = " << message.min() << std::endl;
    std::cout << "Float max = " << message.max() << std::endl;
}

void MomentGenerator::Print(CARTA::MomentImage message) {
    std::cout << "CARTA::MomentImage:" << std::endl;
    std::cout << "file_name = " << message.file_name() << std::endl;
    Print(message.moment_type());
}

void MomentGenerator::Print(CARTA::Moment message) {
    std::cout << "Moment type: ";
    switch (message) {
        case CARTA::Moment::MEAN_OF_THE_SPECTRUM: {
            std::cout << "Mean of the spectrum" << std::endl;
            break;
        }
        case CARTA::Moment::INTEGRATED_OF_THE_SPECTRUM: {
            std::cout << "Integrated of the spectrum" << std::endl;
            break;
        }
        case CARTA::Moment::INTENSITY_WEIGHTED_COORD: {
            std::cout << "Intensity weighted coord" << std::endl;
            break;
        }
        case CARTA::Moment::INTENSITY_WEIGHTED_DISPERSION_OF_THE_COORD: {
            std::cout << "Intensity weighted dispersion of the coord" << std::endl;
            break;
        }
        case CARTA::Moment::MEDIAN_OF_THE_SPECTRUM: {
            std::cout << "Median of the spectrum" << std::endl;
            break;
        }
        case CARTA::Moment::MEDIAN_COORDINATE: {
            std::cout << "Median coordinate" << std::endl;
            break;
        }
        case CARTA::Moment::STD_ABOUT_THE_MEAN_OF_THE_SPECTRUM: {
            std::cout << "STD about the mean of the spectrum" << std::endl;
            break;
        }
        case CARTA::Moment::RMS_OF_THE_SPECTRUM: {
            std::cout << "RMS of the spectrum" << std::endl;
            break;
        }
        case CARTA::Moment::ABS_MEAN_DEVIATION_OF_THE_SPECTRUM: {
            std::cout << "Abs mean deviation of the spectrum" << std::endl;
            break;
        }
        case CARTA::Moment::MAX_OF_THE_SPECTRUM: {
            std::cout << "Max of the spectrum" << std::endl;
            break;
        }
        case CARTA::Moment::COORD_OF_THE_MAX_OF_THE_SPECTRUM: {
            std::cout << "Coord of the max of the spectrum" << std::endl;
            break;
        }
        case CARTA::Moment::MIN_OF_THE_SPECTRUM: {
            std::cout << "Min of the spectrum" << std::endl;
            break;
        }
        case CARTA::Moment::COORD_OF_THE_MIN_OF_THE_SPECTRUM: {
            std::cout << "Coord of the min of the spectrum" << std::endl;
            break;
        }
        default: {
            std::cerr << "Unknown moment type!" << std::endl;
            break;
        }
    }
}

void MomentGenerator::Print(CARTA::MomentAxis message) {
    std::cout << "Moment axis: ";
    switch (message) {
        case CARTA::MomentAxis::RA: {
            std::cout << "RA" << std::endl;
            break;
        }
        case CARTA::MomentAxis::DEC: {
            std::cout << "DEC" << std::endl;
            break;
        }
        case CARTA::MomentAxis::LAT: {
            std::cout << "LAT" << std::endl;
            break;
        }
        case CARTA::MomentAxis::LONG: {
            std::cout << "LONG" << std::endl;
            break;
        }
        case CARTA::MomentAxis::SPECTRAL: {
            std::cout << "SPECTRAL" << std::endl;
            break;
        }
        case CARTA::MomentAxis::STOKES: {
            std::cout << "STOKES" << std::endl;
            break;
        }
        default: {
            std::cerr << "Unknown moment axis!" << std::endl;
            break;
        }
    }
}

void MomentGenerator::Print(CARTA::MomentStokes message) {
    std::cout << "Moment stokes: ";
    switch (message) {
        case CARTA::MomentStokes::I: {
            std::cout << "I" << std::endl;
            break;
        }
        case CARTA::MomentStokes::IV: {
            std::cout << "IV" << std::endl;
            break;
        }
        case CARTA::MomentStokes::IQU: {
            std::cout << "IQU" << std::endl;
            break;
        }
        case CARTA::MomentStokes::IQUV: {
            std::cout << "IQUV" << std::endl;
            break;
        }
        default: {
            std::cerr << "Unknown moment stokes!" << std::endl;
            break;
        }
    }
}

void MomentGenerator::Print(CARTA::MomentMask message) {
    std::cout << "Moment mask: ";
    switch (message) {
        case CARTA::MomentMask::None: {
            std::cout << "None" << std::endl;
            break;
        }
        case CARTA::MomentMask::Include: {
            std::cout << "Include" << std::endl;
            break;
        }
        case CARTA::MomentMask::Exclude: {
            std::cout << "Exclude" << std::endl;
            break;
        }
        default: {
            std::cerr << "Unknown moment mask!" << std::endl;
            break;
        }
    }
}

void MomentGenerator::Print(CARTA::MomentProgress message) {
    std::cout << "CARTA::MomentProgress:" << std::endl;
    std::cout << "progress = " << message.progress() << std::endl;
}