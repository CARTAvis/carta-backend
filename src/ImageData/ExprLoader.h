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
#include <casacore/images/Images/ImageExprParse.h>
#include <casacore/images/Images/ImageOpener.h>
#include <casacore/lattices/LEL/LatticeExprNode.h>

#include "CartaHdf5Image.h"
#include "FileLoader.h"
#include "Util/FileSystem.h"

namespace carta {

template <typename T>
class ExprLoader : public FileLoader<T> {
public:
    ExprLoader(const std::string& filename, const std::string& directory = "");

    void OpenFile(const std::string& hdu) override;
    bool SaveFile(const CARTA::FileType type, const std::string& output_filename, std::string& message) override;
};

template <typename T>
ExprLoader<T>::ExprLoader(const std::string& filename, const std::string& directory) : FileLoader<float>(filename, directory) {}

template <typename T>
void ExprLoader<T>::OpenFile(const std::string& /*hdu*/) {
    if (!this->_image) {
        if (!this->_directory.empty()) {
            // create image from LEL expression stored in _filename
            casacore::String expr(this->_filename);

            // Open HDF5 with CartaHdf5Image
            CartaHdf5Image::RegisterOpenFunction();

            casacore::LatticeExprNode expr_node = casacore::LatticeExprNode(casacore::ImageExprParse::command(expr, this->_directory));
            this->_image.reset(new casacore::ImageExpr<float>(casacore::LatticeExpr<float>(expr_node), expr));
        } else {
            // load LEL image from disk
            fs::path file_path(this->_filename);
            std::string directory = file_path.parent_path().string();
            casacore::JsonKVMap jmap = casacore::JsonParser::parseFile(this->_filename + "/imageexpr.json");
            casacore::String expr = jmap.get("ImageExpr").getString();
            casacore::PtrBlock<const casacore::ImageRegion*> regions;
            casacore::LatticeExprNode node =
                casacore::ImageExprParse::command(expr, casacore::Block<casacore::LatticeExprNode>(), regions, directory);
            this->_image.reset(new casacore::ImageExpr<float>(casacore::LatticeExpr<float>(node), expr, this->_filename, jmap));
        }

        if (!this->_image) {
            throw(casacore::AipsError("Error opening image."));
        }

        this->_image_shape = this->_image->shape();
        this->_num_dims = this->_image_shape.size();
        this->_has_pixel_mask = this->_image->hasPixelMask();
        this->_coord_sys = this->_image->coordinates();
    }
}

template <typename T>
bool ExprLoader<T>::SaveFile(const CARTA::FileType type, const std::string& output_filename, std::string& message) {
    // Save image to disk if CASA file type requested and image was created from LEL expression
    bool success(false);

    if (type != CARTA::FileType::CASA) {
        message = "Cannot save image type in format requested.";
        return success;
    }

    if (!this->_image) {
        OpenFile("");
    }

    casacore::ImageExpr<float>* im = dynamic_cast<casacore::ImageExpr<float>*>(this->_image.get());
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
