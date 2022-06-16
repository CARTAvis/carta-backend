/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# CartaHdf5Image.h : HDF5 Image class derived from casacore::ImageInterface

#ifndef CARTA_BACKEND_IMAGEDATA_CARTAHDF5IMAGE_H_
#define CARTA_BACKEND_IMAGEDATA_CARTAHDF5IMAGE_H_

#include <casacore/casa/Containers/Record.h>
#include <casacore/casa/HDF5/HDF5File.h>
#include <casacore/casa/HDF5/HDF5Group.h>
#include <casacore/images/Images/ImageInterface.h>
#include <casacore/images/Images/MaskSpecifier.h>
#include <casacore/images/Regions/RegionHandlerHDF5.h>
#include <casacore/lattices/Lattices/HDF5Lattice.h>

#include <carta-protobuf/defs.pb.h>

namespace carta {

class CartaHdf5Image : public casacore::ImageInterface<float> {
public:
    // Construct an image from a pre-existing file.
    CartaHdf5Image(const std::string& filename, const std::string& array_name, const std::string& hdu,
        casacore::MaskSpecifier = casacore::MaskSpecifier());
    // Copy constructor
    CartaHdf5Image(const CartaHdf5Image& other);
    ~CartaHdf5Image() override;

    // Function to open a CARTA HDF5 image
    static casacore::LatticeBase* OpenCartaHdf5Image(const casacore::String& name, const casacore::MaskSpecifier& spec);

    // Register the open function in casacore::ImageOpener
    static void RegisterOpenFunction();

    inline const casacore::CountedPtr<casacore::HDF5Group> Group() const {
        return _lattice.group();
    };
    inline const casacore::HDF5Lattice<float> Lattice() {
        return _lattice;
    };

    // implement casacore ImageInterface
    casacore::String imageType() const override;
    casacore::String name(bool stripPath = false) const override;
    casacore::IPosition shape() const override;
    casacore::Bool ok() const override;
    casacore::Bool doGetSlice(casacore::Array<float>& buffer, const casacore::Slicer& section) override;
    void doPutSlice(const casacore::Array<float>& buffer, const casacore::IPosition& where, const casacore::IPosition& stride) override;
    const casacore::LatticeRegion* getRegionPtr() const override;
    casacore::ImageInterface<float>* cloneII() const override;
    void resize(const casacore::TiledShape& newShape) override;
    casacore::uInt advisedMaxPixels() const override;
    casacore::IPosition doNiceCursorShape(casacore::uInt maxPixels) const override;

    // implement functions in other casacore Image classes
    casacore::Bool isMasked() const override;
    casacore::Bool hasPixelMask() const override;
    const casacore::Lattice<bool>& pixelMask() const override;
    casacore::Lattice<bool>& pixelMask() override;
    casacore::Bool doGetMaskSlice(casacore::Array<bool>& buffer, const casacore::Slicer& section) override;

    casacore::Vector<casacore::String> FitsHeaderStrings();

    casacore::DataType internalDataType();

private:
    // Function to return the internal HDF5File object to the RegionHandlerHDF5
    inline static const casacore::CountedPtr<casacore::HDF5File>& GetHdf5File(void* image) {
        CartaHdf5Image* im = static_cast<CartaHdf5Image*>(image);
        return im->_lattice.file();
    }

    void SetUpImage();

    casacore::MaskSpecifier _mask_spec;
    casacore::HDF5Lattice<float> _lattice;
    casacore::Lattice<bool>* _pixel_mask;
    casacore::IPosition _shape;
    casacore::Vector<casacore::String> _fits_header_strings;
};

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_CARTAHDF5IMAGE_H_
