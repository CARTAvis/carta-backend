/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "StokesFilesConnector.h"
#include "Logger/Logger.h"
#include "Util/Casacore.h"
#include "Util/Image.h"

#include <spdlog/fmt/fmt.h>

using namespace carta;

static std::unordered_map<CARTA::PolarizationType, casacore::Stokes::StokesTypes> CasaStokesTypes{
    {CARTA::PolarizationType::I, casacore::Stokes::I}, {CARTA::PolarizationType::Q, casacore::Stokes::Q},
    {CARTA::PolarizationType::U, casacore::Stokes::U}, {CARTA::PolarizationType::V, casacore::Stokes::V},
    {CARTA::PolarizationType::RR, casacore::Stokes::RR}, {CARTA::PolarizationType::LL, casacore::Stokes::LL},
    {CARTA::PolarizationType::RL, casacore::Stokes::RL}, {CARTA::PolarizationType::LR, casacore::Stokes::LR},
    {CARTA::PolarizationType::XX, casacore::Stokes::XX}, {CARTA::PolarizationType::YY, casacore::Stokes::YY},
    {CARTA::PolarizationType::XY, casacore::Stokes::XY}, {CARTA::PolarizationType::YX, casacore::Stokes::YX}};

StokesFilesConnector::StokesFilesConnector(const std::string& _top_level_folder) : _top_level_folder(_top_level_folder) {}

StokesFilesConnector::~StokesFilesConnector() {
    ClearCache();
}

bool StokesFilesConnector::DoConcat(const CARTA::ConcatStokesFiles& message, CARTA::ConcatStokesFilesAck& response,
    std::shared_ptr<casacore::ImageConcat<float>>& concatenated_image, std::string& concatenated_name) {
    ClearCache();
    auto fail_exit = [&](std::string err) {
        response.set_success(false);
        response.set_message(err);
        return false;
    };

    // open files and check their validity
    int stokes_axis(-1);
    std::string err;
    if (!OpenStokesFiles(message, err) || !StokesFilesValid(err, stokes_axis)) {
        return fail_exit(err);
    }

    bool success(true);

    if (stokes_axis < 0) { // create a stokes coordinate and add it to the coordinate system
        std::unordered_map<CARTA::PolarizationType, std::shared_ptr<casacore::ExtendImage<float>>> extended_images;
        std::unordered_map<CARTA::PolarizationType, casacore::CoordinateSystem> coord_sys;
        CARTA::PolarizationType carta_stokes_type;

        // modify the coordinate system and add a stokes coordinate
        for (auto& loader : _loaders) {
            carta_stokes_type = loader.first;
            casacore::CoordinateSystem tmp_coord_sys;

            if (loader.second->GetCoordinateSystem(tmp_coord_sys)) {
                casacore::Vector<casacore::Int> vec(1);
                casacore::Stokes::StokesTypes stokes_type;

                if (GetCasaStokesType(carta_stokes_type, stokes_type)) {
                    vec(0) = stokes_type;                         // set stokes type
                    casacore::StokesCoordinate stokes_coord(vec); // set stokes coordinate
                    tmp_coord_sys.addCoordinate(stokes_coord);    // add stokes coordinate to the coordinate system
                    coord_sys[carta_stokes_type] = tmp_coord_sys; // fill the new coordinate system map
                } else {
                    return fail_exit("Failed to set the stokes coordinate system!");
                }
            } else {
                return fail_exit("Failed to get the coordinate system!");
            }
        }

        // extend the image shapes
        auto& sample_loader = _loaders[carta_stokes_type];
        casacore::IPosition old_image_shape;
        casacore::IPosition new_image_shape;

        if (sample_loader->GetShape(old_image_shape)) {
            new_image_shape.resize(old_image_shape.size() + 1);
            new_image_shape = 1;
            for (int i = 0; i < old_image_shape.size(); ++i) {
                new_image_shape(i) = old_image_shape(i);
            }
        } else {
            return fail_exit("Failed to extend the image shape!");
        }

        // modify the original image and extend the image coordinate system with a stokes coordinate
        for (auto& loader : _loaders) {
            auto stokes_type = loader.first;
            auto image = loader.second->GetImage();
            try {
                extended_images[stokes_type] =
                    std::make_shared<casacore::ExtendImage<float>>(*(image.get()), new_image_shape, coord_sys[stokes_type]);
            } catch (const casacore::AipsError& error) {
                return fail_exit(fmt::format("Failed to extend the image: {}", error.getMesg()));
            }
        }

        // get stokes axis
        stokes_axis = coord_sys[carta_stokes_type].polarizationAxisNumber();

        // concatenate images along the stokes axis
        concatenated_image = std::make_shared<casacore::ImageConcat<float>>(stokes_axis);

        for (int i = 1; i <= StokesTypes.size(); ++i) { // concatenate stokes file in the order I, Q, U, V (i.e., 1, 2, 3 ,4)
            auto stokes_type = static_cast<CARTA::PolarizationType>(i);
            if (extended_images.count(stokes_type)) {
                try {
                    concatenated_image->setImage(*extended_images[stokes_type], casacore::False);
                } catch (const casacore::AipsError& error) {
                    return fail_exit(fmt::format("Failed to concatenate images: {}", error.getMesg()));
                }
            }
        }
    } else { // concatenate images along the stokes axis
        concatenated_image = std::make_shared<casacore::ImageConcat<float>>(stokes_axis);

        for (int i = 1; i <= StokesTypes.size(); ++i) { // concatenate stokes file in the order I, Q, U, V (i.e., 1, 2, 3 ,4)
            auto stokes_type = static_cast<CARTA::PolarizationType>(i);
            if (_loaders.count(stokes_type)) {
                auto image = _loaders[stokes_type]->GetImage();
                const casacore::CoordinateSystem& coordinates = image->coordinates();
                if (!coordinates.hasPolarizationCoordinate()) {
                    return fail_exit("Failed to get the stokes coordinate system!");
                }
                casacore::StokesCoordinate& stokes_coord = const_cast<casacore::StokesCoordinate&>(coordinates.stokesCoordinate());
                if (stokes_coord.stokes().size() != 1) {
                    return fail_exit("Stokes coordinate has no or multiple stokes types!");
                }

                // set stokes type in the stokes coordinate
                casacore::Vector<casacore::Int> vec(1);
                casacore::Stokes::StokesTypes casa_stokes_type;

                if (GetCasaStokesType(stokes_type, casa_stokes_type)) {
                    vec(0) = casa_stokes_type;
                }
                stokes_coord.setStokes(vec);

                try {
                    concatenated_image->setImage(*(image.get()), casacore::False);
                } catch (const casacore::AipsError& error) {
                    return fail_exit(fmt::format("Failed to concatenate images: {}", error.getMesg()));
                }
            }
        }
    }

    // reset the beam information
    if (concatenated_image->imageInfo().hasBeam()) {
        try {
            casacore::ImageInfo image_info = concatenated_image->imageInfo();
            int stokes_size = concatenated_image->shape()[stokes_axis];
            image_info.setAllBeams(image_info.nChannels(), stokes_size, casacore::GaussianBeam());
            unsigned int stokes(0);
            for (int i = 1; i <= StokesTypes.size(); ++i) { // set beam information through stokes types I, Q, U, V (i.e., 1, 2, 3 ,4)
                auto stokes_type = static_cast<CARTA::PolarizationType>(i);
                if (_loaders.count(stokes_type)) {
                    if (_loaders[stokes_type]->GetImage()->imageInfo().hasBeam() && stokes < stokes_size) {
                        casacore::ImageBeamSet beam_set = _loaders[stokes_type]->GetImage()->imageInfo().getBeamSet();
                        casacore::GaussianBeam gaussian_beam;
                        for (unsigned int chan = 0; chan < beam_set.nchan(); ++chan) {
                            gaussian_beam = beam_set.getBeam(chan, stokes);
                            casacore::Quantity major_ax(gaussian_beam.getMajor("arcsec"), "arcsec");
                            casacore::Quantity minor_ax(gaussian_beam.getMinor("arcsec"), "arcsec");
                            casacore::Quantity pa(gaussian_beam.getPA("deg").getValue(), "deg");
                            image_info.setBeam(chan, stokes, major_ax, minor_ax, pa);
                        }
                    } else {
                        spdlog::warn("Stokes type {} has no beam information!", CARTA::PolarizationType_Name(stokes_type));
                    }
                    ++stokes;
                }
            }
            concatenated_image->setImageInfo(image_info);
        } catch (const casacore::AipsError& error) {
            return fail_exit(fmt::format("Failed to reset the beam information for a concatenate image: {}", error.getMesg()));
        }
    }

    concatenated_name = _concatenated_name;
    response.set_success(success);
    response.set_message(err);

    return success;
}

bool StokesFilesConnector::OpenStokesFiles(const CARTA::ConcatStokesFiles& message, std::string& err) {
    if (message.stokes_files_size() < 2) {
        err = "Need at least two files to concatenate!";
        return false;
    }

    int len_head = std::numeric_limits<int>::max(); // max length of the file names in common start from the first char
    int len_tail = std::numeric_limits<int>::max(); // max length of the file names in common start from the last char
    std::string prefix_file_name;                   // the common file name start from the first char
    std::string postfix_file_name;                  // the common file name start from the last char
    casacore::ImageOpener::ImageTypes image_types;  // used to check whether the file type is the same

    // get the common prefix string
    auto common_prefix = [](std::string a, std::string b) {
        if (a.size() > b.size()) {
            std::swap(a, b);
        }
        return std::string(a.begin(), std::mismatch(a.begin(), a.end(), b.begin()).first);
    };

    for (int i = 0; i < message.stokes_files_size(); ++i) {
        auto stokes_file = message.stokes_files(i);
        auto stokes_type = message.stokes_files(i).polarization_type();
        casacore::String hdu(stokes_file.hdu());
        casacore::String full_name(GetResolvedFilename(_top_level_folder, stokes_file.directory(), stokes_file.file()));

        if (_loaders.count(stokes_type)) {
            err = "Duplicate Stokes type found!";
            return false;
        }

        // open an image file
        if (!full_name.empty()) {
            try {
                if (hdu.empty()) { // use first when required
                    hdu = "0";
                }
                _loaders[stokes_type].reset(FileLoader::GetLoader(full_name));
                _loaders[stokes_type]->OpenFile(hdu);
            } catch (casacore::AipsError& ex) {
                err = fmt::format("Failed to open the file: {}", ex.getMesg());
                return false;
            }
        } else {
            err = "File name is empty or does not exist!";
            return false;
        }

        // update the common file name starts from the head or tail
        if (i == 0) {
            image_types = CasacoreImageType(full_name);
            prefix_file_name = stokes_file.file();
            postfix_file_name = prefix_file_name;
            std::reverse(postfix_file_name.begin(), postfix_file_name.end());
        } else {
            if (image_types != CasacoreImageType(full_name)) {
                err = "Different file types can not be concatenated!";
                return false;
            }

            // get the common file name start from the head
            int tmp_len_head = common_prefix(prefix_file_name, stokes_file.file()).size();
            if (tmp_len_head < len_head) {
                len_head = tmp_len_head;
            }
            prefix_file_name = prefix_file_name.substr(0, len_head);

            // get the common file name start from the tail
            std::string tmp_reverse_filename = stokes_file.file();
            std::reverse(tmp_reverse_filename.begin(), tmp_reverse_filename.end());
            int tmp_len_tail = common_prefix(postfix_file_name, tmp_reverse_filename).size();
            if (tmp_len_tail < len_tail) {
                len_tail = tmp_len_tail;
            }
            postfix_file_name = postfix_file_name.substr(0, len_tail);
        }
    }

    _concatenated_name = "";                        // reset the name of concatenated image
    for (int i = 1; i <= StokesTypes.size(); ++i) { // get stokes type in the order I, Q, U, V (i.e., 1, 2, 3 ,4)
        auto stokes_type = static_cast<CARTA::PolarizationType>(i);
        if (_loaders.count(stokes_type)) {
            // update the concatenated image name and insert a new stokes type
            _concatenated_name += CARTA::PolarizationType_Name(stokes_type);
        }
    }

    // check if FITS stokes axis is contiguous
    if (message.stokes_files_size() > 2) {
        int delt = 0;
        int stokes_fits_value = 0;
        for (int i = 0; i < message.stokes_files_size(); ++i) {
            int new_stokes_value = GetStokesValue(message.stokes_files(i).polarization_type());
            int new_stokes_fits_value;
            if (FileInfo::ConvertFitsStokesValue(new_stokes_value, new_stokes_fits_value)) {
                if (stokes_fits_value != 0) {
                    if (delt == 0) {
                        delt = new_stokes_fits_value - stokes_fits_value;
                    } else if (new_stokes_fits_value - stokes_fits_value != delt) {
                        err = fmt::format("Hypercube {} is not allowed!", _concatenated_name);
                        return false;
                    }
                }
                stokes_fits_value = new_stokes_fits_value;
            }
        }
    }

    // set the final name of concatenated image
    std::reverse(postfix_file_name.begin(), postfix_file_name.end());
    _concatenated_name = prefix_file_name + "hypercube_" + _concatenated_name + postfix_file_name;

    return true;
}

bool StokesFilesConnector::StokesFilesValid(std::string& err, int& stokes_axis) {
    if (_loaders.size() < 2) {
        err = "Need at least two files to concatenate!";
        return false;
    }

    casacore::IPosition ref_shape(0);
    int ref_spectral_axis = -1;
    int ref_z_axis = -1;
    int ref_stokes_axis = -1;
    int ref_index = 0;

    for (auto& loader : _loaders) {
        casacore::IPosition shape;
        int spectral_axis;
        int z_axis;

        if (ref_index == 0) {
            loader.second->FindCoordinateAxes(ref_shape, ref_spectral_axis, ref_z_axis, ref_stokes_axis, err);
        } else {
            loader.second->FindCoordinateAxes(shape, spectral_axis, z_axis, stokes_axis, err);
            if ((ref_shape.nelements() != shape.nelements()) || (ref_shape != shape) || (ref_spectral_axis != spectral_axis) ||
                (ref_stokes_axis != stokes_axis)) {
                err = "Image shapes or axes are not consistent!";
                return false;
            }
        }
        ++ref_index;
    }
    return true;
}

bool StokesFilesConnector::GetCasaStokesType(
    const CARTA::PolarizationType& in_stokes_type, casacore::Stokes::StokesTypes& out_stokes_type) {
    if (CasaStokesTypes.count(in_stokes_type)) {
        out_stokes_type = CasaStokesTypes[in_stokes_type];
        return true;
    }
    return false;
}

void StokesFilesConnector::ClearCache() {
    for (auto& loader : _loaders) {
        loader.second.reset();
    }
    _loaders.clear();
    _concatenated_name = "";
}
