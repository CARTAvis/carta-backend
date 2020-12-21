/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ConcatStokesFiles.h"

using namespace carta;

using ImageTypes = casacore::ImageOpener::ImageTypes;

ConcatStokesFiles::ConcatStokesFiles(const std::string& root_folder) : _root_folder(root_folder) {}

ConcatStokesFiles::~ConcatStokesFiles() {
    ClearCache();
}

bool ConcatStokesFiles::DoConcat(const CARTA::ConcatStokesFiles& message, CARTA::ConcatStokesFilesAck& response,
    std::shared_ptr<casacore::ImageConcat<float>>& concat_image, std::string& file_name) {
    ClearCache();

    // open files and check are they valid
    std::string err;
    if (!OpenStokesFiles(message, err) || !StokesFilesValid(err)) {
        response.set_success(false);
        response.set_message(err);
        return false;
    }

    CARTA::StokesType carta_stokes_type = CARTA::StokesType::STOKES_TYPE_NONE;

    // create a stokes coordinate and add it to the coordinate system
    for (auto& loader : _loaders) {
        carta_stokes_type = loader.first;
        casacore::CoordinateSystem coord_sys;

        if (loader.second->GetCoordinateSystem(coord_sys)) {
            casacore::Vector<casacore::Int> vec(1);
            casacore::Stokes::StokesTypes stokes_type;

            if (GetStokesType(carta_stokes_type, stokes_type)) {
                vec(0) = stokes_type;                         // set stokes type
                casacore::StokesCoordinate stokes_coord(vec); // set stokes coordinate
                coord_sys.addCoordinate(stokes_coord);        // add stokes coordinate to the coordinate system
                _coord_sys[carta_stokes_type] = coord_sys;    // fill the new coordinate system map
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

    if (carta_stokes_type == CARTA::StokesType::STOKES_TYPE_NONE) {
        err = "File loader is empty!\n";
        response.set_success(false);
        response.set_message(err);
        return false;
    }

    // extent the image shapes
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

    // extent the image coordinate system with a stokes coordinate
    for (auto& loader : _loaders) {
        auto stokes_type = loader.first;
        auto* image = loader.second->GetImage();
        _extent_images[stokes_type] = std::make_shared<casacore::ExtendImage<float>>(*image, new_image_shape, _coord_sys[stokes_type]);
    }

    // concat images along the stokes axis
    int stokes_axis = _coord_sys[carta_stokes_type].polarizationAxisNumber();
    concat_image = std::make_shared<casacore::ImageConcat<float>>(stokes_axis);

    bool success(true);
    for (auto& extent_image : _extent_images) {
        try {
            concat_image->setImage(*extent_image.second, casacore::False);
        } catch (const casacore::AipsError& error) {
            err = "Fail to concat images:\n" + error.getMesg() + " \n";
            success = false;
            break;
        }
    }

    if (success) { // set concatenate file name
        file_name = _file_name;
    }

    response.set_success(success);
    response.set_message(err);

    return true;
}

bool ConcatStokesFiles::OpenStokesFiles(const CARTA::ConcatStokesFiles& message, std::string& err) {
    if (message.stokes_files_size() < 2) {
        err = "Less than two files to concatenate!\n";
        return false;
    }

    _file_name = "";                                                   // reset the name of the concatenate file
    ImageTypes image_types = ImageTypes::UNKNOWN;                      // used to check whether the file type is the same
    std::unordered_map<CARTA::StokesType, casacore::String> filenames; // used to check the duplication of stokes types assignments

    for (int i = 0; i < message.stokes_files_size(); ++i) {
        auto stokes_file = message.stokes_files(i);
        auto stokes_type = message.stokes_files(i).stokes_type();
        casacore::String hdu(stokes_file.hdu());
        casacore::String full_name(GetResolvedFilename(_root_folder, stokes_file.directory(), stokes_file.file()));

        // concatenate the file name
        _file_name += stokes_file.file();
        if (i < message.stokes_files_size() - 1) {
            _file_name += "/";
        }

        if (i == 0) {
            image_types = CasacoreImageType(full_name);
        } else {
            if (image_types != CasacoreImageType(full_name)) {
                err = "Different file type can not be concatenate!\n";
                return false;
            }
        }

        if (_loaders.count(stokes_type)) {
            err = "Stokes type for a loader is duplicate!\n";
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

        // check the duplication of stokes types assignments
        if (filenames.count(stokes_type)) {
            err = "Stokes type is duplicate!\n";
            return false;
        }

        filenames[stokes_type] = full_name;
    }

    // check the duplication of file names
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

bool ConcatStokesFiles::StokesFilesValid(std::string& err) {
    int ref_index = 0;
    casacore::IPosition ref_shape(0);
    int ref_spectral_axis = -1;
    int ref_stokes_axis = -1;

    for (auto& loader : _loaders) {
        casacore::IPosition shape;
        int spectral_axis;
        int stokes_axis;

        if (ref_index == 0) {
            loader.second->FindCoordinateAxes(ref_shape, ref_spectral_axis, ref_stokes_axis, err);
            if (ref_spectral_axis < 0) {
                err += "Spectral axis does not exist!\n";
                return false;
            }
            if (ref_stokes_axis > 0) {
                err += "Stokes axis already exist!\n";
                return false;
            }
        } else {
            loader.second->FindCoordinateAxes(shape, spectral_axis, stokes_axis, err);
            if (spectral_axis < 0) {
                err += "Spectral axis does not exist!\n";
                return false;
            }
            if (stokes_axis > 0) {
                err += "Stokes axis already exist!\n";
                return false;
            }
            if ((ref_shape.nelements() != shape.nelements()) || (ref_shape != shape) || (ref_spectral_axis != spectral_axis) ||
                (ref_stokes_axis != stokes_axis)) {
                err += "Images shapes or axes are not consistent!\n";
                return false;
            }
        }
        ++ref_index;
    }

    return true;
}

bool ConcatStokesFiles::GetStokesType(const CARTA::StokesType& in_stokes_type, casacore::Stokes::StokesTypes& out_stokes_types) {
    bool success(false);
    switch (in_stokes_type) {
        case CARTA::StokesType::I:
            out_stokes_types = casacore::Stokes::I;
            success = true;
            break;
        case CARTA::StokesType::Q:
            out_stokes_types = casacore::Stokes::Q;
            success = true;
            break;
        case CARTA::StokesType::U:
            out_stokes_types = casacore::Stokes::U;
            success = true;
            break;
        case CARTA::StokesType::V:
            out_stokes_types = casacore::Stokes::V;
            success = true;
            break;
        default:
            break;
    }

    return success;
}

void ConcatStokesFiles::ClearCache() {
    for (auto& loader : _loaders) {
        loader.second.reset();
    }
    _loaders.clear();

    for (auto& extent_image : _extent_images) {
        extent_image.second.reset();
    }
    _extent_images.clear();

    _coord_sys.clear();
}