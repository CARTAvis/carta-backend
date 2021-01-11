/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
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

    bool HasData(FileInfo::Data ds) const override;
    ImageRef GetImage() override;

private:
    std::string _filename;
    std::unique_ptr<casacore::ImageExpr<float>> _image;
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
        _num_dims = _image->shape().size();
    }
}

bool ExprLoader::HasData(FileInfo::Data dl) const {
    switch (dl) {
        case FileInfo::Data::Image:
            return true;
        case FileInfo::Data::XY:
            return _num_dims >= 2;
        case FileInfo::Data::XYZ:
            return _num_dims >= 3;
        case FileInfo::Data::XYZW:
            return _num_dims >= 4;
        case FileInfo::Data::MASK:
            return ((_image != nullptr) && _image->hasPixelMask());
        default:
            break;
    }
    return false;
}

typename ExprLoader::ImageRef ExprLoader::GetImage() {
    return _image.get(); // nullptr if image not opened
}

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_EXPRLOADER_H_
