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
      _progress_callback(progress_callback),
      _use_default_progress_reporter(true),
      _stop(false) {}

MomentGenerator::~MomentGenerator() {}

std::vector<CollapseResult> MomentGenerator::CalculateMoments(
    int file_id, const CARTA::MomentRequest& moment_request, CARTA::MomentResponse& moment_response) {
    // Collapse results
    std::vector<CollapseResult> collapse_results;

    // Set moment axis
    SetMomentAxis(moment_request);

    // Set moment types
    SetMomentTypes(moment_request);

    // Set pixel range
    SetPixelRange(moment_request);

    // Reset an ImageMoments
    ResetImageMoments(moment_request);

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
                        int moment_file_id = (file_id + 1) * 1000 + moment_type;

                        // Fill results
                        std::shared_ptr<casacore::ImageInterface<casacore::Float>> moment_image =
                            dynamic_pointer_cast<casacore::ImageInterface<casacore::Float>>(result_images[i]);
                        collapse_results.push_back(CollapseResult(moment_file_id, out_file_name, moment_image));
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

    // Set is the moment calculation successful or not
    moment_response.set_success(IsSuccess());

    // Get error message if any
    moment_response.set_message(GetErrorMessage());

    return collapse_results;
}

std::vector<CollapseResult> MomentGenerator::CalculateMomentsStopable(
    int file_id, const CARTA::MomentRequest& moment_request, CARTA::MomentResponse& moment_response) {
    // Disable the default progress reporter
    _use_default_progress_reporter = false;

    // Reset the stop flag
    _stop = false;

    // Set moment axis
    SetMomentAxis(moment_request);

    // Set moment types
    SetMomentTypes(moment_request);

    // Set pixel range
    SetPixelRange(moment_request);

    // Initialize the collapse results
    InitCollapseResults(file_id);

    // Get sub image pixel box
    std::vector<std::vector<casacore::Int>> boxes = GetPixelBoxes();

    // Initialize the progress
    float progress = 0.0;

    // Calculate the moment images with respect to the 2D pixel box
    for (int i = 0; i < boxes.size(); ++i) {
        // Check whether to stop the calculation
        if (_stop) {
            break;
        }

        // Report the progress
        if (i > 0) {
            progress = (float)(i) / boxes.size();
            _progress_callback(progress);
        }

        // Reset an ImageMoments
        ResetSubImageMoments(moment_request, boxes[i]);

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

                        // Fill the moment images with respect to the moment types
                        for (int j = 0; j < result_images.size(); ++j) {
                            // Get results
                            std::shared_ptr<casacore::ImageInterface<casacore::Float>> moment_image =
                                dynamic_pointer_cast<casacore::ImageInterface<casacore::Float>>(result_images[j]);

                            // Fill results
                            auto& out_image = _collapse_results[j].image;
                            if (out_image != nullptr) {
                                // Get a copy of the original pixel data
                                casacore::IPosition start(moment_image->shape().size(), 0);
                                casacore::IPosition count(moment_image->shape());
                                casacore::Slicer slice(start, count);
                                casacore::Array<casacore::Float> temp_array;
                                moment_image->doGetSlice(temp_array, slice);

                                // Copy sub-moment image pixels to the resulting image
                                casacore::IPosition out_start(out_image->shape().size(), 0);
                                out_start(_dir_x_axis) = boxes[i][0];
                                out_start(_dir_y_axis) = boxes[i][1];
                                out_image->putSlice(temp_array, out_start);
                                out_image->setUnits(moment_image->units());

                                // Copy sub-moment image mask to the resulting image
                                if (moment_image->hasPixelMask() && out_image->hasPixelMask()) {
                                    casacore::Array<casacore::Bool> moment_image_mask;
                                    moment_image->getMaskSlice(moment_image_mask, slice);
                                    casacore::Lattice<casacore::Bool>& out_image_mask = out_image->pixelMask();
                                    out_image_mask.putSlice(moment_image_mask, out_start);
                                }
                            }
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

    // Report the calculation is done
    _progress_callback(1.0);

    // Set is the moment calculation successful or not
    moment_response.set_success(IsSuccess());

    // Get error message if any
    moment_response.set_message(GetErrorMessage());

    // Clear collapse results if the calculation is interrupted
    if (_stop) {
        _collapse_results.clear();
    }

    return _collapse_results;
}

void MomentGenerator::StopCalculation() {
    _stop = true;
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

casacore::Record MomentGenerator::MakeRegionRecord(const CARTA::MomentRequest& moment_request) {
    casacore::String infile = ""; // Original purpose is to access the region record from the image file

    // Initialize the channels
    int chan_min(moment_request.spectral_range().min());
    int chan_max(moment_request.spectral_range().max());

    // Check if the channel range is logical
    if (chan_max < chan_min) {
        int tmp = chan_max;
        chan_max = chan_min;
        chan_min = tmp;
    }

    casacore::String channels = std::to_string(chan_min) + "~" + std::to_string(chan_max); // Channel range for the moments calculation
    _channels = std::to_string(chan_min) + "_" + std::to_string(chan_max);                 // This is used to set the output file name
    casacore::uInt num_selected_channels = chan_max - chan_min + 1;

    // Set the stokes (not apply this variable yet!)
    casacore::String tmp_stokes = GetStokes(moment_request.stokes()); // "I" , "IV" , "IQU", or "IQUV"

    // Make a region record
    CoordinateSystem coordinate_system = _image->coordinates();
    IPosition pos = _image->shape();
    _image->shape();
    casacore::String region_name;
    casacore::String stokes = ""; // Not available yet, in principle can choose "I" , "IV" , "IQU", or "IQUV"
    casa::CasacRegionManager crm(coordinate_system);
    casacore::String diagnostics;
    casacore::String pixel_box = ""; // Not available yet
    casacore::Record region = crm.fromBCS(diagnostics, num_selected_channels, stokes, NULL, region_name, channels,
        casa::CasacRegionManager::USE_FIRST_STOKES, pixel_box, pos, infile);

    return region;
}

casacore::Record MomentGenerator::MakeRegionRecord(const CARTA::MomentRequest& moment_request, const casacore::String& pixel_box) {
    casacore::String infile = ""; // Original purpose is to access the region record from the image file

    // Initialize the channels
    int chan_min(moment_request.spectral_range().min());
    int chan_max(moment_request.spectral_range().max());

    // Check if the channel range is logical
    if (chan_max < chan_min) {
        int tmp = chan_max;
        chan_max = chan_min;
        chan_min = tmp;
    }

    casacore::String channels = std::to_string(chan_min) + "~" + std::to_string(chan_max); // Channel range for the moments calculation
    _channels = std::to_string(chan_min) + "_" + std::to_string(chan_max);                 // This is used to set the output file name
    casacore::uInt num_selected_channels = chan_max - chan_min + 1;

    // Set the stokes (not apply this variable yet!)
    casacore::String tmp_stokes = GetStokes(moment_request.stokes()); // "I" , "IV" , "IQU", or "IQUV"

    // Make a region record
    CoordinateSystem coordinate_system = _image->coordinates();
    IPosition pos = _image->shape();
    _image->shape();
    casacore::String region_name;
    casacore::String stokes = ""; // Not available yet, in principle can choose "I" , "IV" , "IQU", or "IQUV"
    casa::CasacRegionManager crm(coordinate_system);
    casacore::String diagnostics;
    casacore::Record region = crm.fromBCS(diagnostics, num_selected_channels, stokes, NULL, region_name, channels,
        casa::CasacRegionManager::USE_FIRST_STOKES, pixel_box, pos, infile);

    return region;
}

std::vector<std::vector<casacore::Int>> MomentGenerator::GetPixelBoxes() {
    std::vector<std::vector<casacore::Int>> results;

    const casacore::CoordinateSystem& coord_sys = _image->coordinates();
    casacore::Vector<casacore::Int> dir_axes_numbers;
    if (coord_sys.hasDirectionCoordinate()) {
        dir_axes_numbers = coord_sys.directionAxesNumbers();
    } else {
        dir_axes_numbers = coord_sys.linearAxesNumbers();
    }

    _dir_x_axis = dir_axes_numbers[0];
    _dir_y_axis = dir_axes_numbers[1];

    const casacore::IPosition& image_shape = _image->shape();
    casacore::Vector<casacore::Int> dir_shape(2);
    dir_shape[0] = image_shape[_dir_x_axis];
    dir_shape[1] = image_shape[_dir_y_axis];

    // Calculate 2D pixel boxes
    int len_x = dir_shape[0] / SUB_SECTION_COUNT;
    int len_y = dir_shape[1] / SUB_SECTION_COUNT;
    int res_x = dir_shape[0] % SUB_SECTION_COUNT;
    int res_y = dir_shape[1] % SUB_SECTION_COUNT;

    for (int i = 0; i < SUB_SECTION_COUNT; ++i) {
        for (int j = 0; j < SUB_SECTION_COUNT; ++j) {
            std::vector<casacore::Int> box_corners;
            box_corners.resize(4);
            box_corners[0] = i * len_x;
            box_corners[1] = j * len_y;
            box_corners[2] = (i + 1) * len_x - 1;
            box_corners[3] = (j + 1) * len_y - 1;
            if (i == (SUB_SECTION_COUNT - 1)) {
                box_corners[2] += res_x;
            }
            if (j == (SUB_SECTION_COUNT - 1)) {
                box_corners[3] += res_y;
            }
            results.push_back(box_corners);
        }
    }

    return results;
}

void MomentGenerator::ResetImageMoments(const CARTA::MomentRequest& moment_request) {
    // Make a region record
    casacore::Record region = MakeRegionRecord(moment_request);

    // Make a sub image interface
    casacore::String empty("");
    std::shared_ptr<const SubImage<casacore::Float>> sub_image =
        casa::SubImageFactory<casacore::Float>::createSubImageRO(*_image, region, empty, NULL);
    _sub_image.reset(new SubImage<casacore::Float>(*sub_image));
    casacore::LogOrigin log("MomentGenerator", "MomentGenerator", WHERE);
    casacore::LogIO os(log);

    // Make an ImageMoments object (and overwrite the output file if it already exists)
    _image_moments.reset(new casa::ImageMoments<casacore::Float>(casacore::SubImage<casacore::Float>(*_sub_image), os, true));

    if (_use_default_progress_reporter) {
        // Set moment calculation progress monitor
        _image_moments->setProgressMonitor(this);
    }
}

void MomentGenerator::ResetSubImageMoments(const CARTA::MomentRequest& moment_request, const std::vector<casacore::Int>& pixel_box) {
    // Make a pixel box string
    casacore::String box = std::to_string(pixel_box[0]) + ", " + std::to_string(pixel_box[1]) + ", " + std::to_string(pixel_box[2]) + ", " +
                           std::to_string(pixel_box[3]);

    // Make a region record
    casacore::Record region = MakeRegionRecord(moment_request, box);

    // Make a sub image interface
    casacore::String empty("");
    std::shared_ptr<const SubImage<casacore::Float>> sub_image =
        casa::SubImageFactory<casacore::Float>::createSubImageRO(*_image, region, empty, NULL);
    _sub_image.reset(new SubImage<casacore::Float>(*sub_image));
    casacore::LogOrigin log("MomentGenerator", "MomentGenerator", WHERE);
    casacore::LogIO os(log);

    // Make an ImageMoments object (and overwrite the output file if it already exists)
    _image_moments.reset(new casa::ImageMoments<casacore::Float>(casacore::SubImage<casacore::Float>(*_sub_image), os, true));

    if (_use_default_progress_reporter) {
        // Set moment calculation progress monitor
        _image_moments->setProgressMonitor(this);
    }
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

casacore::String MomentGenerator::GetMomentSuffix(casacore::Int moment) {
    casacore::String suffix;
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

casacore::String MomentGenerator::GetStokes(CARTA::MomentStokes moment_stokes) {
    casacore::String stokes("");
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
    if (_use_default_progress_reporter) {
        // Initialize the progress parameters
        _total_steps = count;
        _progress = 0.0;
        _pre_progress = 0.0;
    }
}

void MomentGenerator::setStepsCompleted(int count) {
    if (_use_default_progress_reporter) {
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
}

void MomentGenerator::done() {}

void MomentGenerator::InitCollapseResults(int file_id) {
    // Clear the vector if it is not empty
    if (!_collapse_results.empty()) {
        _collapse_results.clear();
    }

    // Get the file base name w/o path string
    casacore::String out_file = GetOutputFileName();
    std::size_t found = out_file.find_last_of("/");
    std::string file_base_name = out_file.substr(found + 1);

    // Initialize the collapse results vector
    for (int i = 0; i < _moments.size(); ++i) {
        // Set temp moment file name
        std::string moment_suffix = GetMomentSuffix(_moments[i]);
        std::string out_file_name = file_base_name + "." + moment_suffix;

        // Set temp moment file id
        int moment_type = _moments[i];
        int moment_file_id = (file_id + 1) * 1000 + moment_type;

        // Make an moment image interface
        casacore::IPosition out_image_shape;
        const auto out_coord_sys = MakeOutputCoordinates(out_image_shape, _image->coordinates(), _image->shape(), _axis, false);
        std::shared_ptr<casacore::ImageInterface<casacore::Float>> moment_image =
            std::make_shared<casacore::TempImage<casacore::Float>>(casacore::TiledShape(out_image_shape), out_coord_sys);

        // Copy the original image info
        moment_image->setMiscInfo(_image->miscInfo());
        moment_image->setImageInfo(_image->imageInfo());
        moment_image->appendLog(_image->logger());
        moment_image->makeMask("mask0", true, true);

        // Fill the vector
        _collapse_results.push_back(CollapseResult(moment_file_id, out_file_name, moment_image));
    }
}

casacore::CoordinateSystem MomentGenerator::MakeOutputCoordinates(casacore::IPosition& out_shape,
    const casacore::CoordinateSystem& in_coor_sys, const casacore::IPosition& in_shape, casacore::Int moment_axis,
    casacore::Bool remove_axis) {
    casacore::CoordinateSystem out_coor_sys;
    out_coor_sys.setObsInfo(in_coor_sys.obsInfo());

    // Find the casacore::Coordinate corresponding to the moment axis
    casacore::Int coord;
    casacore::Int axis_in_coord;
    in_coor_sys.findPixelAxis(coord, axis_in_coord, moment_axis);
    const casacore::Coordinate& c = in_coor_sys.coordinate(coord);

    // Find the number of axes
    if (remove_axis) {
        // Shape with moment axis removed
        casacore::uInt dim_in = in_shape.size();
        casacore::uInt dim_out = dim_in - 1;
        out_shape.resize(dim_out);
        casacore::uInt k = 0;
        for (casacore::uInt i = 0; i < dim_in; ++i) {
            if (casacore::Int(i) != moment_axis) {
                out_shape(k) = in_shape(i);
                ++k;
            }
        }
        if (c.nPixelAxes() == 1 && c.nWorldAxes() == 1) {
            // We can physically remove the coordinate and axis
            for (casacore::uInt i = 0; i < in_coor_sys.nCoordinates(); ++i) {
                // If this coordinate is not the moment axis coordinate,
                // and it has not been virtually removed in the input
                // we add it to the output.  We don't cope with transposed
                // CoordinateSystems yet.
                auto pixel_axes = in_coor_sys.pixelAxes(i);
                auto world_axes = in_coor_sys.worldAxes(i);
                if (casacore::Int(i) != coord && pixel_axes[0] >= 0 && world_axes[0] >= 0) {
                    out_coor_sys.addCoordinate(in_coor_sys.coordinate(i));
                }
            }
        } else {
            // Remove just world and pixel axis but not the coordinate
            out_coor_sys = in_coor_sys;
            casacore::Int world_axis = out_coor_sys.pixelAxisToWorldAxis(moment_axis);
            out_coor_sys.removeWorldAxis(world_axis, in_coor_sys.referenceValue()(world_axis));
        }
    } else {
        // Retain the casacore::Coordinate and give the moment axis  shape 1.
        out_shape.resize(0);
        out_shape = in_shape;
        out_shape(moment_axis) = 1;
        out_coor_sys = in_coor_sys;
    }
    return out_coor_sys;
}

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