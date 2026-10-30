/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_CARTAZARRIMAGE_H_
#define CARTA_SRC_IMAGEDATA_CARTAZARRIMAGE_H_

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <carta-zarr/carta_zarr.h>
#include <casacore/images/Images/ImageInterface.h>
#include <casacore/lattices/Lattices/TiledShape.h>

#include "ZarrCasacore.h"

namespace carta {

// One line of a Zarr image's storage layout, for the file info. `has_shape` says the value lists
// the image's axes in the image's order, so it can be labeled like the image shape.
struct StorageEntry {
    std::string name;
    std::string value;
    bool has_shape = false;
};

// An XRADIO image of the sky or of the aperture plane, in CARTA's axis order: spatial X, spatial Y,
// spectral, polarization. A time axis is only accepted with a single time.
class CartaZarrImage : public casacore::ImageInterface<float> {
public:
    explicit CartaZarrImage(const std::string& filename, const std::string& image_id = {});
    CartaZarrImage(const CartaZarrImage& other) = default;

    casacore::String imageType() const override;
    casacore::String name(bool stripPath = false) const override;
    casacore::IPosition shape() const override;
    casacore::Bool ok() const override;
    casacore::DataType dataType() const override;
    casacore::DataType InternalDataType() const;
    casacore::Bool doGetSlice(casacore::Array<float>& buffer, const casacore::Slicer& section) override;
    casacore::IPosition doNiceCursorShape(casacore::uInt max_pixels) const override;
    // The shape of one chunk of the store in CARTA's order: what is decoded together
    casacore::IPosition ChunkShape() const;
    // The pixels of `section`, in CARTA's order, as carta-zarr read them. Throws casacore::AipsError for a
    // section that is not fixed or not of the image's rank.
    carta::zarr::Result<std::size_t> Read(casacore::Array<float>& buffer, const casacore::Slicer& section) const;
    void doPutSlice(const casacore::Array<float>& buffer, const casacore::IPosition& where, const casacore::IPosition& stride) override;
    const casacore::LatticeRegion* getRegionPtr() const override;
    void resize(const casacore::TiledShape& newShape) override;
    casacore::ImageInterface<float>* cloneII() const override;

    // carta-zarr reads a flagged or absent pixel as NaN, so the mask is the finiteness of the pixels
    casacore::Bool isMasked() const override;
    casacore::Bool hasPixelMask() const override;
    // The mask as a read-only lattice, for casacore code that asks for the whole mask, such as concatenating
    // the Stokes files of a hypercube. It reads each section as doGetMaskSlice does.
    const casacore::Lattice<casacore::Bool>& pixelMask() const override;
    casacore::Lattice<casacore::Bool>& pixelMask() override;
    casacore::Bool doGetMaskSlice(casacore::Array<casacore::Bool>& buffer, const casacore::Slicer& section) override;
    // Bytes of mask held for the mask read that follows a pixel read
    std::size_t MaskCacheBytes() const;

    std::vector<StorageEntry> GetStorageInfo() const;
    // carta-zarr's diagnostics on the image and the notes made while describing it to casacore
    const std::vector<ZarrNote>& Notes() const {
        return _notes;
    }

private:
    void SetUpImage();
    // The beams at the first time, if the store has a table casacore accepts
    std::optional<casacore::ImageBeamSet> ReadBeamSet();
    // Logs each note and keeps it for the file info
    void AddNotes(std::vector<ZarrNote> notes);

    // What the mask reads keep. A copy of the image starts with none of it.
    struct MaskCache {
        std::mutex mutex;
        // The section of the last pixel read and its mask: casacore asks for a section's pixels and then
        // its mask, so the mask is computed with the pixels instead of reading them again
        std::optional<casacore::Slicer> section;
        casacore::Array<casacore::Bool> mask;
        // The mask as a lattice, made when pixelMask is first called
        std::unique_ptr<casacore::Lattice<casacore::Bool>> lattice;

        MaskCache() = default;
        MaskCache(const MaskCache& /*other*/) {}
    };
    mutable MaskCache _mask_cache;

    std::string _filename;
    std::optional<carta::zarr::Image> _zarr_image;
    // In CARTA's order; see CartaShape. An image CARTA cannot display is not opened.
    casacore::IPosition _shape;
    std::vector<ZarrNote> _notes;
};

} // namespace carta

#endif // CARTA_SRC_IMAGEDATA_CARTAZARRIMAGE_H_
