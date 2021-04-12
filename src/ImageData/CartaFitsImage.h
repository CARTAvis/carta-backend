/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

//# CartaFitsImage.h : FITS Image class derived from casacore::ImageInterface for images not supported by casacore,
//# including compressed and Int64

#ifndef CARTA_BACKEND_IMAGEDATA_CARTAFITSIMAGE_H_
#define CARTA_BACKEND_IMAGEDATA_CARTAFITSIMAGE_H_

#include <casacore/casa/Utilities/DataType.h>
#include <casacore/images/Images/ImageInterface.h>
#include <casacore/tables/DataMan/TiledFileAccess.h>

#include "../Cfitsio.h"

namespace carta {

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

private:
    fitsfile* OpenFile();
    void CloseFile(fitsfile* fptr);
    void CloseFileIfError(fitsfile* fptr, const int& status, const std::string& error);

    void SetUpImage();
    void GetFitsHeaders(int& nkeys, std::string& hdrstr);

    // casacore ImageFITSConverter workaround
    casacore::CoordinateSystem SetUpCoordinateSystem(int nkeys, const std::string& header_str, casacore::RecordInterface& unused_headers);
    bool AddDirectionCoordinate(casacore::CoordinateSystem& coord_sys, const ::wcsprm& wcs, std::vector<int>& direction_axes);
    void SetUnusedHeaderRec(char* header, casacore::RecordInterface& unused_headers);
    void AddObsInfo(casacore::CoordinateSystem& coord_sys, casacore::RecordInterface& header_rec);

    std::string _filename;
    unsigned int _hdu;

    // FITS header values
    bool _is_compressed;
    casacore::IPosition _shape;
    int _datatype; // bitpix value
    bool _has_blanks;
    unsigned char _uchar_blank;
    short _short_blank;
    int _int_blank;
    LONGLONG _longlong_blank;

    casacore::Lattice<bool>* _pixel_mask;
};

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_CARTAFITSIMAGE_H_
