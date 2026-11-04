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

// One line of a Zarr image's storage layout, for the file info.
// `is_shape` is true for a chunk shape or a shard shape, for example "[512, 512, 4, 1]".
// The numbers of such a shape are in the same axis order as the image shape.
// So the file info labels them with the same axis names as the image shape.
struct StorageEntry {
    std::string name;
    std::string value;
    bool is_shape = false;
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
    // The shape of one chunk of the store, in CARTA's order. carta-zarr decodes a whole chunk at a time.
    casacore::IPosition ChunkShape() const;
    // Reads the pixels of `section` into `buffer`, with carta-zarr's read options and progress callback.
    // The section is in CARTA's order. A pixel that is flagged, absent or not finite becomes NaN.
    // Returns carta-zarr's error if the read fails.
    // Throws casacore::AipsError if the section is not fixed or does not have the image's rank.
    // It is virtual, so that a test can count the reads of an image.
    virtual carta::zarr::Result<std::size_t> Read(casacore::Array<float>& buffer, const casacore::Slicer& section,
        const carta::zarr::ReadOptions& options = {}, const carta::zarr::ProgressCallback& progress = {}) const;
    // The carta-zarr image, for the work that casacore has no call for, such as reductions and chunk sizes.
    // Its axes are in CARTA's order, the same as in Read.
    const carta::zarr::Image& ZarrImage() const {
        return *_zarr_image;
    }
    void doPutSlice(const casacore::Array<float>& buffer, const casacore::IPosition& where, const casacore::IPosition& stride) override;
    const casacore::LatticeRegion* getRegionPtr() const override;
    void resize(const casacore::TiledShape& newShape) override;
    casacore::ImageInterface<float>* cloneII() const override;

    // The image always has a mask. The mask is true where a pixel is finite.
    // carta-zarr reads flagged and absent pixels as NaN, so the mask is false for them.
    casacore::Bool isMasked() const override;
    casacore::Bool hasPixelMask() const override;
    // The mask as a read-only lattice. casacore code that needs the whole mask uses it,
    // for example to concatenate the Stokes files of a hypercube.
    // The lattice reads each section through doGetMaskSlice, so the whole mask is never held in memory.
    const casacore::Lattice<casacore::Bool>& pixelMask() const override;
    casacore::Lattice<casacore::Bool>& pixelMask() override;
    casacore::Bool doGetMaskSlice(casacore::Array<casacore::Bool>& buffer, const casacore::Slicer& section) override;
    // The size in bytes of the mask kept from the last pixel read. Tests use it to check that the mask is released.
    std::size_t MaskCacheBytes() const;

    std::vector<StorageEntry> GetStorageInfo() const;
    // carta-zarr's diagnostics on the image and the notes made while describing it to casacore
    const std::vector<ZarrNote>& Notes() const {
        return _notes;
    }

private:
    void SetUpImage();
    // The beams of the first time slice, unless the store has no beam table or one casacore refuses
    std::optional<casacore::ImageBeamSet> ReadBeamSet();
    // Logs each note and keeps it for the file info
    void AddNotes(std::vector<ZarrNote> notes);

    // The state that the mask reads keep. A copy of the image starts empty, because the copy constructor copies none of it.
    struct MaskCache {
        std::mutex mutex;
        // The section of the last pixel read, and its mask.
        // casacore reads a section's pixels and then its mask, so we keep the mask from the pixel read.
        std::optional<casacore::Slicer> section;
        casacore::Array<casacore::Bool> mask;
        // The mask lattice. pixelMask makes it on its first call.
        std::unique_ptr<casacore::Lattice<casacore::Bool>> lattice;

        MaskCache() = default;
        MaskCache(const MaskCache& /*other*/) {}
    };
    mutable MaskCache _mask_cache;

    std::string _filename;
    std::optional<carta::zarr::Image> _zarr_image;
    // CARTA's four axes in CARTA's order; see CartaShape in ZarrStores.h
    casacore::IPosition _shape;
    std::vector<ZarrNote> _notes;
};

} // namespace carta

#endif // CARTA_SRC_IMAGEDATA_CARTAZARRIMAGE_H_
