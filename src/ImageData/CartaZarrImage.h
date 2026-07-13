/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# CartaZarrImage.h : Zarr image class derived from casacore::ImageInterface

#ifndef CARTA_SRC_IMAGEDATA_CARTAZARRIMAGE_H_
#define CARTA_SRC_IMAGEDATA_CARTAZARRIMAGE_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <casacore/casa/Arrays/Vector.h>
#include <casacore/images/Images/ImageInterface.h>
#include <casacore/lattices/Lattices/TiledShape.h>

namespace carta {

class ZarrImage;

class CartaZarrImage : public casacore::ImageInterface<float> {
public:
    // If image_name is empty, ZARR_DEFAULT_IMAGE_ARRAY is used.
    explicit CartaZarrImage(const std::string& filename, const std::string& image_name = std::string());
    CartaZarrImage(const CartaZarrImage& other);
    ~CartaZarrImage() override;

    casacore::String imageType() const override;
    casacore::String name(casacore::Bool stripPath = false) const override;
    casacore::IPosition shape() const override;
    casacore::Bool ok() const override;
    casacore::DataType dataType() const override;
    casacore::Bool doGetSlice(casacore::Array<float>& buffer, const casacore::Slicer& section) override;
    void doPutSlice(const casacore::Array<float>& buffer, const casacore::IPosition& where, const casacore::IPosition& stride) override;
    const casacore::LatticeRegion* getRegionPtr() const override;
    void resize(const casacore::TiledShape& newShape) override;
    casacore::ImageInterface<float>* cloneII() const override;

    casacore::Bool isMasked() const override;
    casacore::Bool hasPixelMask() const override;
    const casacore::Lattice<casacore::Bool>& pixelMask() const override;
    casacore::Lattice<casacore::Bool>& pixelMask() override;
    casacore::Bool doGetMaskSlice(casacore::Array<casacore::Bool>& buffer, const casacore::Slicer& section) override;

    casacore::Vector<casacore::String> FitsHeaderStrings();
    casacore::DataType InternalDataType() const;

    // Storage encoding info (compressor, compression level, chunk/shard shape) as label/value pairs.
    std::vector<std::pair<std::string, std::string>> GetStorageInfo();

private:
    std::shared_ptr<ZarrImage> _zarr_image;
    casacore::IPosition _shape;
    casacore::String _name;

    casacore::Vector<casacore::String> _fits_header_strings;

    void SetUpImage();
    void SetBeams();
};

} // namespace carta

#endif // CARTA_SRC_IMAGEDATA_CARTAZARRIMAGE_H_
