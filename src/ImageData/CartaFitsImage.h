/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2024 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# CartaFitsImage.h : FITS Image class derived from casacore::ImageInterface for images not supported by casacore,
//# including compressed and Int64

#ifndef CARTA_SRC_IMAGEDATA_CARTAFITSIMAGE_H_
#define CARTA_SRC_IMAGEDATA_CARTAFITSIMAGE_H_

#include <casacore/casa/Utilities/DataType.h>
#include <casacore/images/Images/ImageInfo.h>
#include <casacore/images/Images/ImageInterface.h>
#include <casacore/lattices/Lattices/TiledShape.h>

#include <fitsio.h>

#include "../Logger/Logger.h"

namespace carta {

static std::unordered_map<int, casacore::DataType> bitpix_types(
    {{8, casacore::DataType::TpChar}, {16, casacore::DataType::TpShort}, {32, casacore::DataType::TpInt}, {64, casacore::DataType::TpInt64},
        {-32, casacore::DataType::TpFloat}, {-64, casacore::DataType::TpDouble}});

class CartaFitsImage : public casacore::ImageInterface<float> {
public:
    // Construct an image from a pre-existing file.
    CartaFitsImage(const std::string& filename, unsigned int hdu = 0);
    // Copy constructor
    CartaFitsImage(const CartaFitsImage& other);
    ~CartaFitsImage() override;

    // implement casacore ImageInterface
    casacore::String imageType() const override;
    casacore::String name(bool stripPath = false) const override;
    casacore::IPosition shape() const override;
    casacore::Bool ok() const override;
    casacore::DataType dataType() const override;
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

    casacore::DataType internalDataType() const;

    // Headers accessors
    casacore::Vector<casacore::String> FitsHeaderStrings();

private:
    // Uses _fptr (nullptr when file is closed)
    fitsfile* OpenFile();
    void CloseFile();
    void CloseFileIfError(const int& status, const std::string& error);
    void CheckFileStatus(fitsfile* fptr);

    void SetUpImage();
    void GetFitsHeaderString(int& nheaders, std::string& hdrstr);
    void SetFitsHeaderStrings(int nheaders, const std::string& header);

    // casacore ImageFITSConverter workaround
    casacore::CoordinateSystem SetCoordinateSystem(
        int nheaders, const std::string& header_str, casacore::RecordInterface& unused_headers, int& stokes_fits_value);
    bool AddDirectionCoordinate(casacore::CoordinateSystem& coord_sys, const ::wcsprm& wcs, std::vector<int>& direction_axes);
    bool AddStokesCoordinate(casacore::CoordinateSystem& coord_sys, const ::wcsprm& wcs, const casacore::IPosition& shape,
        int& stokes_fits_value, int& stokes_axis);
    bool AddSpectralCoordinate(casacore::CoordinateSystem& coord_sys, const ::wcsprm& wcs, const casacore::IPosition& shape,
        int& spectral_axis, int& linear_spectral_axis);
    casacore::MFrequency::Types GetFrequencyType(const ::wcsprm& wcs_spectral);
    bool AddLinearCoordinate(casacore::CoordinateSystem& coord_sys, const ::wcsprm& wcs, std::vector<int>& linear_axes);
    void SetCoordSysOrder(casacore::CoordinateSystem& coord_sys, int naxes, std::vector<int>& special_axes, std::vector<int>& lin_axes);
    void SetHeaderRec(char* header, casacore::RecordInterface& header_rec);
    void ReadBeamsTable(casacore::ImageInfo& image_info);
    void AddObsInfo(casacore::CoordinateSystem& coord_sys, casacore::RecordInterface& header_rec);

    // Pixel mask
    void SetPixelMask();
    bool doGetNanMaskSlice(casacore::Array<bool>& buffer, const casacore::Slicer& section);

    template <typename T>
    bool GetDataSubset(int datatype, const casacore::Slicer& section, casacore::Array<float>& buffer);
    template <typename T>
    bool GetPixelMask(int datatype, const casacore::IPosition& shape, casacore::ArrayLattice<bool>& mask);
    template <typename T>
    bool GetNanPixelMask(casacore::ArrayLattice<bool>& mask);

    std::string _filename;
    unsigned int _hdu;

    // File pointer for open file; nullptr when closed.
    // cfitsio docs: "Different threads should not share the same 'fitsfile' pointer to read an opened FITS file
    // unless locks are placed around the calls to the CFITSIO reading routines."
    fitsfile* _fptr;
    std::mutex _fptr_mutex;

    // FITS header values
    bool _is_compressed;
    casacore::IPosition _shape;
    int _bitpix;
    int _equiv_bitpix;
    bool _has_blanks;
    casacore::Vector<casacore::String> _all_header_strings;
    casacore::Vector<casacore::String> _image_header_strings;

    casacore::Lattice<bool>* _pixel_mask;
    casacore::TiledShape _tiled_shape;

    // Whether is a copy of the other CartaFitsImage
    bool _is_copy;
};

} // namespace carta

#include "CartaFitsImage.tcc"

#endif // CARTA_SRC_IMAGEDATA_CARTAFITSIMAGE_H_
