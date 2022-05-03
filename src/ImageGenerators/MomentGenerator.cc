/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "MomentGenerator.h"

#include "../Logger/Logger.h"
#include "ImageGenerator.h"

using namespace carta;

using IM = ImageMoments<casacore::Float>;

MomentGenerator::MomentGenerator(const casacore::String& filename, casacore::ImageInterface<float>* image)
    : _filename(filename), _image(image), _sub_image(nullptr), _image_moments(nullptr), _success(false), _cancel(false) {
    SetMomentTypeMaps();
}

bool MomentGenerator::CalculateMoments(int file_id, const casacore::ImageRegion& image_region, int spectral_axis, int stokes_axis,
    const GeneratorProgressCallback& progress_callback, const CARTA::MomentRequest& moment_request, CARTA::MomentResponse& moment_response,
    std::vector<GeneratedImage>& collapse_results, const RegionState& region_state) {
    _spectral_axis = spectral_axis;
    _stokes_axis = stokes_axis;
    _progress_callback = progress_callback;
    _success = false;
    _cancel = false;

    // Set moment axis
    SetMomentAxis(moment_request);

    // Set pixel range
    SetPixelRange(moment_request);

    // Set moment types
    SetMomentTypes(moment_request);

    // Reset an ImageMoments
    ResetImageMoments(image_region);

    // Calculate moments
    try {
        // Start the timer
        _start_time = std::chrono::high_resolution_clock::now();

        // Reset the first progress report
        _first_report_made = false;

        if (!_image_moments->setMoments(_moments)) {
            _error_msg = _image_moments->errorMessage();
        } else {
            if (!_image_moments->setMomentAxis(_axis)) {
                _error_msg = _image_moments->errorMessage();
            } else {
                casacore::Bool do_temp = true;
                casacore::Bool remove_axis = false;
                casacore::String file_base_name = GetInputFileName() + ".moment";
                try {
                    _image_moments->setInExCludeRange(_include_pix, _exclude_pix);

                    // Do calculations and save collapse results in the memory
                    auto result_images = _image_moments->createMoments(do_temp, file_base_name, remove_axis);

                    for (int i = 0; i < result_images.size(); ++i) {
                        // Set temp moment file name
                        std::string moment_suffix = GetMomentSuffix(_moments[i]);
                        std::string out_file_name = std::string(file_base_name) + "." + moment_suffix;

                        // Set a temp moment file Id. Todo: find another better way to assign the temp file Id
                        int moment_type = _moments[i];
                        int moment_file_id = (file_id + 1) * ID_MULTIPLIER + moment_type + 1;

                        // Fill results
                        std::shared_ptr<casacore::ImageInterface<casacore::Float>> moment_image =
                            dynamic_pointer_cast<casacore::ImageInterface<casacore::Float>>(result_images[i]);
                        AddHistory(moment_image, moment_request, region_state);
                        collapse_results.push_back(GeneratedImage(moment_file_id, out_file_name, moment_image));
                    }
                    _success = true;
                } catch (const AipsError& x) {
                    _error_msg = x.getMesg();
                }
            }
        }
    } catch (AipsError& error) {
        _error_msg = error.getLastMessage();
    }

    // Set is the moment calculation successful or not
    moment_response.set_success(IsSuccess());

    // Set the moment calculation is cancelled or not
    moment_response.set_cancel(IsCancelled());

    // Get error message if any
    moment_response.set_message(GetErrorMessage());

    return !collapse_results.empty();
}

void MomentGenerator::StopCalculation() {
    if (_image_moments) {
        _image_moments->StopCalculation();
        _cancel = true;
    }
}

void MomentGenerator::SetMomentAxis(const CARTA::MomentRequest& moment_request) {
    if (moment_request.axis() == CARTA::MomentAxis::SPECTRAL) {
        _axis = _spectral_axis;
    } else if (moment_request.axis() == CARTA::MomentAxis::STOKES) {
        _axis = _stokes_axis;
    } else {
        spdlog::error("Unsupported moment axis: {}", moment_request.axis());
    }
}

void MomentGenerator::SetMomentTypes(const CARTA::MomentRequest& moment_request) {
    int moments_size(moment_request.moments_size());

    // Check whether to remove the median coordinate moment type
    bool remove_median_coord(false);
    for (int i = 0; i < moments_size; ++i) {
        if (moment_request.moments(i) == CARTA::Moment::MEDIAN_COORDINATE) {
            if (((_include_pix.size() == 2) && (_include_pix[0] * _include_pix[1] < 0)) || (_include_pix.empty() && _exclude_pix.empty())) {
                remove_median_coord = true;
                break;
            }
        }
    }

    if (remove_median_coord && moments_size > 0) {
        _moments.resize(moments_size - 1);
    } else {
        _moments.resize(moments_size);
    }

    // Fill moment types to calculate
    for (int i = 0, j = 0; i < moments_size; ++i) {
        if (moment_request.moments(i) != CARTA::Moment::MEDIAN_COORDINATE || !remove_median_coord) {
            _moments[j++] = GetMomentMode(moment_request.moments(i));
        }
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
        _include_pix.resize(2);
        _include_pix[0] = pixel_min;
        _include_pix[1] = pixel_max;
        // Clear the exclusive array
        _exclude_pix.resize(0);
    } else if (moment_mask == CARTA::MomentMask::Exclude) {
        _exclude_pix.resize(2);
        _exclude_pix[0] = pixel_min;
        _exclude_pix[1] = pixel_max;
        // Clear the inclusive array
        _include_pix.resize(0);
    } else {
        // Clear inclusive and exclusive array
        _include_pix.resize(0);
        _exclude_pix.resize(0);
    }
}

void MomentGenerator::ResetImageMoments(const casacore::ImageRegion& image_region) {
    // Reset the sub-image
    _sub_image.reset(new casacore::SubImage<casacore::Float>(*_image, image_region));

    casacore::LogOrigin log("MomentGenerator", "MomentGenerator", WHERE);
    casacore::LogIO os(log);

    // Make an ImageMoments object and overwrite the output file if it already exists
    _image_moments.reset(new IM(casacore::SubImage<casacore::Float>(*_sub_image), os, this, true));
}

int MomentGenerator::GetMomentMode(CARTA::Moment moment) {
    if (_moment_map.count(moment)) {
        return _moment_map[moment];
    } else {
        spdlog::error("Unknown moment mode: {}", moment);
        return -1;
    }
}

casacore::String MomentGenerator::GetMomentSuffix(casacore::Int moment) {
    auto moment_type = static_cast<IM::MomentTypes>(moment);
    if (_moment_suffix_map.count(moment_type)) {
        return _moment_suffix_map[moment_type];
    } else {
        spdlog::error("Unknown moment mode: {}", moment);
        return "";
    }
}

casacore::String MomentGenerator::GetInputFileName() {
    // Store moment images in a temporary folder
    casacore::String result(_filename);
    size_t found = result.find_last_of("/");
    if (found) {
        result.replace(0, found + 1, "");
    }
    return result;
}

bool MomentGenerator::IsSuccess() const {
    return _success;
}

bool MomentGenerator::IsCancelled() const {
    return _cancel;
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
    if (_progress > 1.0) {
        _progress = 1.0;
    }

    if (!_first_report_made) {
        auto current_time = std::chrono::high_resolution_clock::now();
        auto dt = std::chrono::duration<double, std::milli>(current_time - _start_time).count();
        if (dt >= FIRST_PROGRESS_AFTER_MILLI_SECS) {
            _progress_callback(_progress);
            _first_report_made = true;
        }
    }

    // Update the progress report every percent
    if ((_progress - _pre_progress) >= PROGRESS_REPORT_INTERVAL) {
        _progress_callback(_progress);
        _pre_progress = _progress;
        if (!_first_report_made) {
            _first_report_made = true;
        }
    }
}

void MomentGenerator::done() {}

inline void MomentGenerator::SetMomentTypeMaps() {
    _moment_map[CARTA::Moment::MEAN_OF_THE_SPECTRUM] = IM::AVERAGE;
    _moment_map[CARTA::Moment::INTEGRATED_OF_THE_SPECTRUM] = IM::INTEGRATED;
    _moment_map[CARTA::Moment::INTENSITY_WEIGHTED_COORD] = IM::WEIGHTED_MEAN_COORDINATE;
    _moment_map[CARTA::Moment::INTENSITY_WEIGHTED_DISPERSION_OF_THE_COORD] = IM::WEIGHTED_DISPERSION_COORDINATE;
    _moment_map[CARTA::Moment::MEDIAN_OF_THE_SPECTRUM] = IM::MEDIAN;
    _moment_map[CARTA::Moment::MEDIAN_COORDINATE] = IM::MEDIAN_COORDINATE;
    _moment_map[CARTA::Moment::STD_ABOUT_THE_MEAN_OF_THE_SPECTRUM] = IM::STANDARD_DEVIATION;
    _moment_map[CARTA::Moment::RMS_OF_THE_SPECTRUM] = IM::RMS;
    _moment_map[CARTA::Moment::ABS_MEAN_DEVIATION_OF_THE_SPECTRUM] = IM::ABS_MEAN_DEVIATION;
    _moment_map[CARTA::Moment::MAX_OF_THE_SPECTRUM] = IM::MAXIMUM;
    _moment_map[CARTA::Moment::COORD_OF_THE_MAX_OF_THE_SPECTRUM] = IM::MAXIMUM_COORDINATE;
    _moment_map[CARTA::Moment::MIN_OF_THE_SPECTRUM] = IM::MINIMUM;
    _moment_map[CARTA::Moment::COORD_OF_THE_MIN_OF_THE_SPECTRUM] = IM::MINIMUM_COORDINATE;

    _moment_suffix_map[IM::AVERAGE] = "average";
    _moment_suffix_map[IM::INTEGRATED] = "integrated";
    _moment_suffix_map[IM::WEIGHTED_MEAN_COORDINATE] = "weighted_coord";
    _moment_suffix_map[IM::WEIGHTED_DISPERSION_COORDINATE] = "weighted_dispersion_coord";
    _moment_suffix_map[IM::MEDIAN] = "median";
    _moment_suffix_map[IM::MEDIAN_COORDINATE] = "median_coord";
    _moment_suffix_map[IM::STANDARD_DEVIATION] = "standard_deviation";
    _moment_suffix_map[IM::RMS] = "rms";
    _moment_suffix_map[IM::ABS_MEAN_DEVIATION] = "abs_mean_dev";
    _moment_suffix_map[IM::MAXIMUM] = "maximum";
    _moment_suffix_map[IM::MAXIMUM_COORDINATE] = "maximum_coord";
    _moment_suffix_map[IM::MINIMUM] = "minimum";
    _moment_suffix_map[IM::MINIMUM_COORDINATE] = "minimum_coord";
}

void MomentGenerator::AddHistory(const std::shared_ptr<casacore::ImageInterface<casacore::Float>>& moment_image,
    const CARTA::MomentRequest& moment_request, const RegionState& region_state) {
    std::shared_ptr<casacore::CoordinateSystem> coord_sys =
        std::shared_ptr<casacore::CoordinateSystem>(static_cast<casacore::CoordinateSystem*>(_image->coordinates().clone()));

    // Set input image info
    std::string input_image = fmt::format("Input image: {}\n", GetInputFileName());

    // Set region info
    auto control_points = region_state.control_points;
    std::string region_info;
    if (region_state.type == CARTA::RegionType::RECTANGLE) {
        region_info = "Region: rotbox[";
    } else if (region_state.type == CARTA::RegionType::ELLIPSE) {
        region_info = "Region: ellipse[";
    } else if (region_state.type == CARTA::RegionType::POLYGON) {
        region_info = "Region: poly[";
    }

    for (auto point : control_points) {
        if (region_info.back() == ']') {
            region_info += ", ";
        }
        region_info += fmt::format("[{:.4f}pix, {:.4f}pix]", point.x(), point.y());
    }

    if (region_state.type == CARTA::RegionType::POLYGON) {
        region_info += "]\n";
    } else {
        region_info += fmt::format(", {:.4f}deg]\n", region_state.rotation);
    }

    // Set spectral range info
    int z_min = moment_request.spectral_range().min();
    int z_max = moment_request.spectral_range().max();
    std::string spectral_range = fmt::format("Spectral range: [{}, {}](channel)", z_min, z_max);
    if (coord_sys && coord_sys->hasSpectralAxis()) {
        auto spectral_coord = coord_sys->spectralCoordinate();
        casacore::Vector<casacore::String> spectral_units = spectral_coord.worldAxisUnits();
        spectral_units(0) = ((spectral_units(0) == "Hz") ? "GHz" : spectral_units(0));
        spectral_coord.setWorldAxisUnits(spectral_units);
        std::string velocity_units("km/s");
        spectral_coord.setVelocity(velocity_units);
        std::vector<double> frequencies(2);
        std::vector<double> velocities(2);

        if (spectral_coord.toWorld(frequencies[0], z_min) && spectral_coord.toWorld(frequencies[1], z_max)) {
            spectral_range += fmt::format(", [{:.4f}, {:.4f}]({})", frequencies[0], frequencies[1], spectral_units(0));
        }
        if ((spectral_coord.restFrequency() != 0) && spectral_coord.pixelToVelocity(velocities[0], z_min) &&
            spectral_coord.pixelToVelocity(velocities[1], z_max)) {
            spectral_range += fmt::format(", [{:.4f}, {:.4f}]({})", velocities[0], velocities[1], velocity_units);
        }
    }
    spectral_range += "\n";

    // Set mask info
    float pixel_min = moment_request.pixel_range().min();
    float pixel_max = moment_request.pixel_range().max();
    std::string mask = "Mask: ";
    if (moment_request.mask() == CARTA::Include) {
        mask += fmt::format("include pixels [{:.4f}, {:.4f}]({})\n", pixel_min, pixel_max, _image->units().getName());
    } else if (moment_request.mask() == CARTA::Exclude) {
        mask += fmt::format("exclude pixels [{:.4f}, {:.4f}]({})\n", pixel_min, pixel_max, _image->units().getName());
    } else {
        mask += "none\n";
    }

    casacore::LoggerHolder logger;
    logger.logio() << "CARTA MOMENT MAP GENERATOR LOG\n" << input_image << region_info << spectral_range << mask << casacore::LogIO::POST;
    moment_image->appendLog(logger);
}
