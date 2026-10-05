/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2026 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_CARTAZARRIMAGE_H_
#define CARTA_SRC_IMAGEDATA_CARTAZARRIMAGE_H_

#include <carta-zarr/carta_zarr.h>

#include "CartaZarrAxes.h"
#include "ZarrNotes.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <casacore/casa/Arrays/Array.h>
#include <casacore/images/Images/ImageInterface.h>
#include <casacore/lattices/Lattices/TiledShape.h>

namespace carta {

// One line of a Zarr image's storage layout, for the file-info panel.
//
// `has_shape` says the value is this image's own axes in this image's own order, so whoever
// displays it can label those axes the way it labels the image shape. Carried here rather than
// recovered from the value, which the consumer used to do by looking for a leading '[': a contract
// between two modules that nothing stated and nothing checked.
struct StorageEntry {
    std::string name;
    std::string value;
    bool has_shape = false;
};

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
    // The shape of a chunk of the store, in CARTA's axis order: what the image decodes together, where
    // the cursor advice above is grown past it. Empty when there is no image.
    casacore::IPosition ChunkShape() const;
    // The pixels of `section`, which is in CARTA's axis order. What the library said is returned
    // as it said it: the casacore overrides below throw on anything but success, because that is
    // how casacore hears of a failure, and ZarrLoader reads it as it reads its walks.
    carta::zarr::Result<std::size_t> Read(casacore::Array<float>& buffer, const casacore::Slicer& section,
        const carta::zarr::ReadOptions& options = {},
        const carta::zarr::ProgressCallback& progress = {}) const;
    // What a Read of `section` asks of the library: the request in the library's axis order, and the
    // options it reads through -- this image's own cache when one is held. For a caller that hands the
    // read on rather than making it, as reading ahead does; see PlaneReadAhead.
    std::pair<carta::zarr::ReadRequest, carta::zarr::ReadOptions> LibraryRead(const casacore::Slicer& section) const;
    // The library's own image, for the walks casacore has no call for: a Lattice is asked for
    // pixels, and every reduction it offers is one region at a time. Those walks name their axes by
    // role, so unlike Read they need nothing from this image's mapping onto CARTA's order, and
    // passing them through here only turned the library's Result into something else on the way.
    const carta::zarr::Image& Library() const;
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

    // Makes this image's reads, and those of every copy made of it from now on, keep what they decode
    // in a cache of their own rather than the session's, while one is held. The function returned
    // holds one: given a size, it makes a pool that large and returns what keeps it, and letting go
    // of that frees the pool and returns the reads to the session's. One held at a time.
    //
    // For a walk that comes back to what it decoded -- a moment, whose neighbouring slabs share chunks
    // -- read through a copy, so that the image the session displays keeps its own cache throughout.
    // Copies made before this call do not share it, so the copy to call it on is the walk's own.
    std::function<std::shared_ptr<void>(std::uint64_t bytes)> OwnCache();
    // The size of the cache of its own these reads keep what they decode in, while one is held.
    std::optional<std::size_t> OwnCacheBytes() const;

    std::vector<StorageEntry> GetStorageInfo() const;
    // How many bytes of mask the image is holding for a mask read to come.
    std::size_t MaskCacheBytes() const;
    // What a reader should know about this image's values: carta-zarr's diagnostics on it and what
    // the backend had to say while describing it to casacore. See ZarrNote.
    const std::vector<ZarrNote>& Notes() const {
        return _notes;
    }

private:
    void SetUpImage();
    void SetBeams();
    // The library image, or an exception naming `where` when there is none.
    const carta::zarr::Image& Opened(const char* where) const;
    // `options`, reading through the cache of its own held for this image's reads when there is one
    // and the caller named none.
    carta::zarr::ReadOptions ThroughOwnCache(const carta::zarr::ReadOptions& options) const;

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
    // Set with _zarr_image, and never without it: an image CARTA cannot display is not opened.
    std::optional<CartaZarrAxes> _axes;
    std::vector<ZarrNote> _notes;
    carta::zarr::ImageDescriptor _descriptor;
    casacore::IPosition _shape;

    // The cache OwnCache holds, shared by the copies made after it was called and empty until then.
    struct CacheOfItsOwn {
        std::mutex mutex;
        std::optional<carta::zarr::CachePool> pool;
    };
    std::shared_ptr<CacheOfItsOwn> _own_cache;
};

}  // namespace carta

#endif  // CARTA_SRC_IMAGEDATA_CARTAZARRIMAGE_H_
