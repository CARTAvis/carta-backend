/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_CARTAZARRIMAGE_H_
#define CARTA_SRC_IMAGEDATA_CARTAZARRIMAGE_H_

#include <carta-zarr/carta_zarr.h>

#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <casacore/casa/Arrays/Array.h>
#include <casacore/images/Images/ImageInterface.h>
#include <casacore/lattices/Lattices/TiledShape.h>

namespace carta {

// XRADIO sky images use the backend's canonical pixel order:
// [spatial X, spatial Y, spectral frequency, polarization]. The backend currently supports
// only a singleton XRADIO time axis, which is represented as observation metadata rather than
// an image dimension.
class CartaZarrImage : public casacore::ImageInterface<float> {
public:
    explicit CartaZarrImage(const std::string& filename, const std::string& image_id = {});
    CartaZarrImage(const CartaZarrImage& other);
    ~CartaZarrImage() override;

    casacore::String imageType() const override;
    casacore::String name(bool stripPath = false) const override;
    casacore::IPosition shape() const override;
    casacore::Bool ok() const override;
    casacore::DataType dataType() const override;
    casacore::DataType InternalDataType() const;
    casacore::Bool doGetSlice(casacore::Array<float>& buffer, const casacore::Slicer& section) override;
    casacore::IPosition doNiceCursorShape(casacore::uInt max_pixels) const override;
    bool Read(casacore::Array<float>& buffer, const casacore::Slicer& section,
        const carta::zarr::ReadOptions& options = {}) const;
    void doPutSlice(const casacore::Array<float>& buffer, const casacore::IPosition& where,
        const casacore::IPosition& stride) override;
    const casacore::LatticeRegion* getRegionPtr() const override;
    void resize(const casacore::TiledShape& newShape) override;
    casacore::ImageInterface<float>* cloneII() const override;

    casacore::Bool isMasked() const override;
    casacore::Bool hasPixelMask() const override;
    const casacore::Lattice<casacore::Bool>& pixelMask() const override;
    casacore::Lattice<casacore::Bool>& pixelMask() override;
    casacore::Bool doGetMaskSlice(casacore::Array<casacore::Bool>& buffer,
        const casacore::Slicer& section) override;

    std::vector<std::pair<std::string, std::string>> GetStorageInfo() const;

private:
    void SetUpImage();
    void SetBeams();
    static carta::zarr::ReadRequest MakeReadRequest(const casacore::Slicer& section);

    // The finiteness mask of the most recent doGetSlice. casacore asks for a cursor's pixels and
    // then that same cursor's mask, so computing the mask while the pixels are still hot turns the
    // second request into a lookup instead of a second decode-and-transpose. The image is
    // read-only, so a section that matches is always still valid.
    mutable std::mutex _mask_cache_mutex;
    mutable casacore::IPosition _mask_cache_start;
    mutable casacore::IPosition _mask_cache_length;
    mutable casacore::IPosition _mask_cache_stride;
    mutable casacore::Array<casacore::Bool> _mask_cache;

    std::string _filename;
    std::string _image_id;
    std::optional<carta::zarr::Image> _zarr_image;
    carta::zarr::ImageDescriptor _descriptor;
    casacore::IPosition _shape;
};

}  // namespace carta

#endif  // CARTA_SRC_IMAGEDATA_CARTAZARRIMAGE_H_
