/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGEDATA_EXPRLOADER_H_
#define CARTA_BACKEND_IMAGEDATA_EXPRLOADER_H_

#include <casacore/casa/Json/JsonKVMap.h>
#include <casacore/casa/Json/JsonParser.h>
#include <casacore/images/Images/ImageExpr.h>
#include <casacore/images/Images/ImageExprGram.h>
#include <casacore/images/Images/ImageExprParse.h>
#include <casacore/lattices/LEL/LatticeExprNode.h>

#include "CartaHdf5Image.h"
#include "FileLoader.h"
#include "Util/FileSystem.h"

namespace carta {

class ExprLoader : public FileLoader {
public:
    ExprLoader(const std::string& filename, const std::string& directory = "");

    void OpenFile(const std::string& hdu) override;
    bool SaveFile(const CARTA::FileType type, const std::string& output_filename, std::string& message) override;
};

ExprLoader::ExprLoader(const std::string& filename, const std::string& directory) : FileLoader(filename, directory) {}

void ExprLoader::OpenFile(const std::string& /*hdu*/) {
    if (!_image) {
        if (!_directory.empty()) {
            // create image from LEL expression stored in _filename
            casacore::String expr(_filename);
            casacore::LatticeExprNode expr_node;

            try {
                expr_node = casacore::LatticeExprNode(casacore::ImageExprParse::command(expr, _directory));
            } catch (const casacore::AipsError& err) {
                casacore::String error = err.getMesg();

                if (error.contains("hdf5")) {
                    throw(casacore::AipsError("LEL expressions not supported for HDF5 images."));
                } else {
                    throw(err);
                }
            }

            _image.reset(new casacore::ImageExpr<float>(casacore::LatticeExpr<float>(expr_node), expr));
        } else {
            // load LEL image from disk
            fs::path file_path(_filename);
            std::string directory = file_path.parent_path().string();
            casacore::JsonKVMap jmap = casacore::JsonParser::parseFile(_filename + "/imageexpr.json");
            casacore::String expr = jmap.get("ImageExpr").getString();
            casacore::PtrBlock<const casacore::ImageRegion*> regions;
            casacore::LatticeExprNode node =
                casacore::ImageExprParse::command(expr, casacore::Block<casacore::LatticeExprNode>(), regions, directory);
            _image.reset(new casacore::ImageExpr<float>(casacore::LatticeExpr<float>(node), expr, _filename, jmap));
        }

        if (!_image) {
            throw(casacore::AipsError("Error opening image."));
        }

        _image_shape = _image->shape();
        _num_dims = _image_shape.size();
        _has_pixel_mask = _image->hasPixelMask();
        _coord_sys = _image->coordinates();
    }
}

bool ExprLoader::SaveFile(const CARTA::FileType type, const std::string& output_filename, std::string& message) {
    // Save image to disk if CASA file type requested and image was created from LEL expression
    bool success(false);

    if (type != CARTA::FileType::CASA) {
        message = "Cannot save image type in format requested.";
        return success;
    }

    if (!_image) {
        OpenFile("");
    }

    casacore::ImageExpr<float>* im = dynamic_cast<casacore::ImageExpr<float>*>(_image.get());
    try {
        im->save(output_filename);
        success = true;
    } catch (const casacore::AipsError& err) {
        message = err.getMesg();
    }

    return success;
}

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_EXPRLOADER_H_
