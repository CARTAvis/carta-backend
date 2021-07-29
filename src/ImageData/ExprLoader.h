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
#include <casacore/lattices/LEL/LatticeExprNode.h>

#include "FileLoader.h"

namespace carta {

class ExprLoader : public FileLoader {
public:
    ExprLoader(const std::string& filename);

    void OpenFile(const std::string& hdu) override;
};

ExprLoader::ExprLoader(const std::string& filename) : FileLoader(filename) {}

void ExprLoader::OpenFile(const std::string& /*hdu*/) {
    if (!_image) {
        casacore::JsonKVMap _jmap = casacore::JsonParser::parseFile(_filename + "/imageexpr.json");
        casacore::String _expr = _jmap.get("ImageExpr").getString();
        casacore::PtrBlock<const casacore::ImageRegion*> _regions;
        casacore::LatticeExprNode _node = casacore::ImageExprParse::command(_expr, casacore::Block<casacore::LatticeExprNode>(), _regions);
        _image.reset(new casacore::ImageExpr<float>(_node, _expr, _filename, _jmap));

        if (!_image) {
            throw(casacore::AipsError("Error opening image"));
        }

        _image_shape = _image->shape();
        _num_dims = _image_shape.size();
        _has_pixel_mask = _image->hasPixelMask();
        _coord_sys = _image->coordinates();
    }
}

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_EXPRLOADER_H_
