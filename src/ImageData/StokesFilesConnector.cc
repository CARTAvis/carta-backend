/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "StokesFilesConnector.h"

using namespace carta;

using ImageTypes = casacore::ImageOpener::ImageTypes;

StokesFilesConnector::StokesFilesConnector(const std::string& _top_level_folder) : _top_level_folder(_top_level_folder) {}

StokesFilesConnector::~StokesFilesConnector() {
    ClearCache();
}

bool StokesFilesConnector::DoConcat(const CARTA::ConcatStokesFiles& message, CARTA::ConcatStokesFilesAck& response,
    std::shared_ptr<casacore::ImageConcat<float>>& concatenate_image, std::string& concatenate_name) {
    ClearCache();
    int stokes_axis(-1);

    // open files and check are they valid
    std::string err;
    if (!OpenStokesFiles(message, err) || !StokesFilesValid(err, stokes_axis)) {
        response.set_success(false);
        response.set_message(err);
        return false;
    }

    bool success(true);

    if (stokes_axis < 0) { // create a stokes coordinate and add it to the coordinate system
        std::unordered_map<CARTA::StokesType, std::shared_ptr<casacore::ExtendImage<float>>> extended_images;
        std::unordered_map<CARTA::StokesType, casacore::CoordinateSystem> coord_sys;
        CARTA::StokesType carta_stokes_type;

        for (auto& loader : _loaders) {
            carta_stokes_type = loader.first;
            casacore::CoordinateSystem tmp_coord_sys;

            if (loader.second->GetCoordinateSystem(tmp_coord_sys)) {
                casacore::Vector<casacore::Int> vec(1);
                casacore::Stokes::StokesTypes stokes_type;

                if (GetStokesType(carta_stokes_type, stokes_type)) {
                    vec(0) = stokes_type;                         // set stokes type
                    casacore::StokesCoordinate stokes_coord(vec); // set stokes coordinate
                    tmp_coord_sys.addCoordinate(stokes_coord);    // add stokes coordinate to the coordinate system
                    coord_sys[carta_stokes_type] = tmp_coord_sys; // fill the new coordinate system map
                } else {
                    err = "Fail to set the stokes coordinate system!\n";
                    response.set_success(false);
                    response.set_message(err);
                    return false;
                }
            } else {
                err = "Fail to get the coordinate system!\n";
                response.set_success(false);
                response.set_message(err);
                return false;
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
            err = "Fail to set the new image shape!\n";
            response.set_success(false);
            response.set_message(err);
            return false;
        }

        // extend the image coordinate system with a stokes coordinate
        for (auto& loader : _loaders) {
            auto stokes_type = loader.first;
            auto* image = loader.second->GetImage();
            extended_images[stokes_type] = std::make_shared<casacore::ExtendImage<float>>(*image, new_image_shape, coord_sys[stokes_type]);
        }

        // get stokes axis
        stokes_axis = coord_sys[carta_stokes_type].polarizationAxisNumber();

        // concat images along the stokes axis
        concatenate_image = std::make_shared<casacore::ImageConcat<float>>(stokes_axis);

        for (int i = 1; i <= 4; ++i) { // concatenate stokes file in the order I, Q, U, V (i.e., 1, 2, 3 ,4)
            auto stokes_type = static_cast<CARTA::StokesType>(i);
            if (extended_images.count(stokes_type)) {
                try {
                    concatenate_image->setImage(*extended_images[stokes_type], casacore::False);
                } catch (const casacore::AipsError& error) {
                    err = "Fail to concat images: " + error.getMesg() + "\n";
                    success = false;
                    break;
                }
            }
        }
    } else { // concat images along the stokes axis
        concatenate_image = std::make_shared<casacore::ImageConcat<float>>(stokes_axis);

        for (int i = 1; i <= 4; ++i) { // concatenate stokes file in the order I, Q, U, V (i.e., 1, 2, 3 ,4)
            auto stokes_type = static_cast<CARTA::StokesType>(i);
            if (_loaders.count(stokes_type)) {
                try {
                    // change the original stokes types
                    casacore::StokesCoordinate& stokes_coord =
                        const_cast<casacore::StokesCoordinate&>(_loaders[stokes_type]->GetImage()->coordinates().stokesCoordinate());
                    casacore::Vector<casacore::Int> vec(1);
                    vec(0) = stokes_type;
                    stokes_coord.setStokes(vec);

                    concatenate_image->setImage(*_loaders[stokes_type]->GetImage(), casacore::False);
                } catch (const casacore::AipsError& error) {
                    err = "Fail to concat images: " + error.getMesg() + "\n";
                    success = false;
                    break;
                }
            }
        }
    }

    if (success) { // set concatenate file name
        concatenate_name = _concatenate_name;
    }

    response.set_success(success);
    response.set_message(err);

    return true;
}

bool StokesFilesConnector::OpenStokesFiles(const CARTA::ConcatStokesFiles& message, std::string& err) {
    if (message.stokes_files_size() < 2) {
        err = "Needs at least two files to concatenate!\n";
        return false;
    }

    int pos0 = std::numeric_limits<int>::max();                        // max length of the file names in common start from the first char
    int pos1 = std::numeric_limits<int>::max();                        // max length of the file names in common start from the last char
    std::string prefix_file_name;                                      // the common file name start from the first char
    std::string postfix_file_name;                                     // the common file name start from the last char
    _concatenate_name = "hypercube_";                                  // name of the concatenate file
    ImageTypes image_types;                                            // used to check whether the file type is the same
    std::unordered_map<CARTA::StokesType, casacore::String> filenames; // used to check whether the stokes type assignment is duplicate

    for (int i = 0; i < message.stokes_files_size(); ++i) {
        auto stokes_file = message.stokes_files(i);
        auto stokes_type = message.stokes_files(i).stokes_type();
        casacore::String hdu(stokes_file.hdu());
        casacore::String full_name(GetResolvedFilename(_top_level_folder, stokes_file.directory(), stokes_file.file()));

        _concatenate_name += CARTA::StokesType_Name(stokes_type);

        if (i == 0) {
            image_types = CasacoreImageType(full_name);
            prefix_file_name = stokes_file.file();
            postfix_file_name = prefix_file_name;
            std::reverse(postfix_file_name.begin(), postfix_file_name.end());
        } else {
            if (image_types != CasacoreImageType(full_name)) {
                err = "Different file types can not be concatenated!\n";
                return false;
            }

            // get the common file name start from the head
            int tmp_pos0 = StringComparison(prefix_file_name, stokes_file.file());
            if (tmp_pos0 < pos0) {
                pos0 = tmp_pos0;
            }
            prefix_file_name = prefix_file_name.substr(0, pos0);

            // get the common file name start from the tail
            std::string tmp_reverse_filename = stokes_file.file();
            std::reverse(tmp_reverse_filename.begin(), tmp_reverse_filename.end());
            int tmp_pos1 = StringComparison(postfix_file_name, tmp_reverse_filename);
            if (tmp_pos1 < pos1) {
                pos1 = tmp_pos1;
            }
            postfix_file_name = postfix_file_name.substr(0, pos1);
        }

        if (_loaders.count(stokes_type)) {
            err = "Stokes type for is duplicate!\n";
            return false;
        }

        // open the file
        if (!full_name.empty()) {
            try {
                if (hdu.empty()) { // use first when required
                    hdu = "0";
                }
                _loaders[stokes_type].reset(carta::FileLoader::GetLoader(full_name));
                _loaders[stokes_type]->OpenFile(hdu);
            } catch (casacore::AipsError& ex) {
                err = "Fail to open the file: " + ex.getMesg();
                return false;
            }
        } else {
            err = "File name is empty or does not exist!\n";
            return false;
        }

        // check whether the stokes type assignment is duplicate
        if (filenames.count(stokes_type)) {
            err = "Stokes type is duplicate!\n";
            return false;
        }

        filenames[stokes_type] = full_name;
    }

    // set the name of concatenated image
    std::reverse(postfix_file_name.begin(), postfix_file_name.end());
    _concatenate_name = prefix_file_name + _concatenate_name + postfix_file_name;

    // check whether the file name is duplicate
    std::set<casacore::String> filenames_set;
    for (auto filename : filenames) {
        filenames_set.insert(filename.second);
    }

    if (filenames_set.size() != filenames.size()) {
        err = "File name is duplicate!\n";
        return false;
    }

    return true;
}

bool StokesFilesConnector::StokesFilesValid(std::string& err, int& stokes_axis) {
    if (_loaders.size() < 2) {
        err = "Needs at least two files to concatenate!\n";
        return false;
    }

    casacore::IPosition ref_shape(0);
    int ref_spectral_axis = -1;
    int ref_stokes_axis = -1;
    int ref_index = 0;

    for (auto& loader : _loaders) {
        casacore::IPosition shape;
        int spectral_axis;

        if (ref_index == 0) {
            loader.second->FindCoordinateAxes(ref_shape, ref_spectral_axis, ref_stokes_axis, err);
        } else {
            loader.second->FindCoordinateAxes(shape, spectral_axis, stokes_axis, err);
            if ((ref_shape.nelements() != shape.nelements()) || (ref_shape != shape) || (ref_spectral_axis != spectral_axis) ||
                (ref_stokes_axis != stokes_axis)) {
                err = "Images shapes or axes are not consistent!\n";
                return false;
            }
        }
        ++ref_index;
    }

    return true;
}

bool StokesFilesConnector::GetStokesType(const CARTA::StokesType& in_stokes_type, casacore::Stokes::StokesTypes& out_stokes_type) {
    bool success(false);
    switch (in_stokes_type) {
        case CARTA::StokesType::I:
            out_stokes_type = casacore::Stokes::I;
            success = true;
            break;
        case CARTA::StokesType::Q:
            out_stokes_type = casacore::Stokes::Q;
            success = true;
            break;
        case CARTA::StokesType::U:
            out_stokes_type = casacore::Stokes::U;
            success = true;
            break;
        case CARTA::StokesType::V:
            out_stokes_type = casacore::Stokes::V;
            success = true;
            break;
        default:
            break;
    }
    return success;
}

void StokesFilesConnector::ClearCache() {
    for (auto& loader : _loaders) {
        loader.second.reset();
    }
    _loaders.clear();
    _concatenate_name = "";
}

int StokesFilesConnector::StringComparison(const std::string& str1, const std::string& str2) {
    int pos(0);
    if (!str1.empty() && !str2.empty()) {
        if (str1.size() < str2.size()) {
            for (int i = 0; i < str1.size(); ++i) {
                if (str1.at(pos) == str2.at(pos)) {
                    ++pos;
                } else {
                    break;
                }
            }
        } else {
            for (int i = 0; i < str2.size(); ++i) {
                if (str1.at(pos) == str2.at(pos)) {
                    ++pos;
                } else {
                    break;
                }
            }
        }
    }
    return pos;
}