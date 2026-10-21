/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_ZARRSTORES_H_
#define CARTA_SRC_IMAGEDATA_ZARRSTORES_H_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <carta-zarr/carta_zarr.h>
#include <casacore/casa/Arrays/IPosition.h>

namespace carta {

constexpr std::chrono::seconds ZARR_STORES_TTL(30);
constexpr std::size_t ZARR_STORES_SIZE = 512;

struct ZarrStoreSize {
    int64_t bytes = 0;
    // The uncompressed size declared in the metadata, because the bytes on disk could not be measured
    bool declared = false;
};

// Whether CARTA can display an image, and its shape in CARTA's order: spatial X, spatial Y, spectral,
// polarization. carta-zarr reads in the image's logical order, so that order must be CARTA's, then at most a
// time axis of length one, with every length above zero and within CARTA's 32-bit limit. None, with the
// reason, otherwise. The file list asks with the listing's axes before offering an image, and the image asks
// with its descriptor before opening, so the two agree.
std::optional<casacore::IPosition> CartaShape(const std::vector<carta::zarr::AxisDescriptor>& axes, std::string& reason);
// The same for an opened image, which must also have spectral and polarization coordinates, and a direction
// or, for an image of the aperture plane, an aperture coordinate
std::optional<casacore::IPosition> CartaShape(const carta::zarr::ImageDescriptor& descriptor, std::string& reason);

// The images of a dataset that CARTA offers, from the listing alone. The file list shows them, and an image
// opened without an id opens the first.
struct ZarrOffer {
    struct Refused {
        std::string id;
        std::string reason;
    };

    // The images CARTA can display, in the library's order (SKY first)
    std::vector<std::string> ids;
    // Every other image, with the reason
    std::vector<Refused> refused;
    // Why no image can be opened, if none is offered: CARTA's reason first, then carta-zarr's, then that there is no image
    std::string why_not_openable;
};

ZarrOffer OfferedImages(const carta::zarr::DatasetDescriptor& dataset);

// An image of a store, opened as CARTA displays it
struct ZarrOpenedImage {
    carta::zarr::Image image;
    // In CARTA's order; see CartaShape
    casacore::IPosition shape;
};

// What a path is, as far as Zarr goes
struct ZarrStoreProbe {
    // A Zarr image store: one that opened, or an XRADIO one that will not open but is still listed as an
    // image so that opening it says why. Not a plain folder, a file, or a Zarr store of something else.
    bool is_zarr = false;
    // Set when the store opened
    std::optional<carta::zarr::Dataset> dataset;
    // The images CARTA offers, the default first
    std::vector<std::string> offered;
    // Why no image can be opened: the store will not open, or it offers none
    std::string why_not_openable;

    // The size of the store, measured on first use and then kept; zero for a store that will not open
    ZarrStoreSize Size() const;
    // Image `id`, or without one the first offered, if CARTA can display it; none with the reason otherwise
    std::optional<ZarrOpenedImage> OpenImage(const std::string& id, std::string& reason) const;

    std::string path;

private:
    mutable std::mutex _size_mutex;
    mutable std::optional<ZarrStoreSize> _size;
};

// The Zarr stores this process has opened, kept for ZARR_STORES_TTL. The file list, the file info, the
// choice of loader and the image all ask whether a path is a Zarr image store, what it offers and how
// large it is, so each path is opened and sized once and shared.
//
// An entry also expires when the directory's own timestamp changes. That only catches changes to its
// entry list, not writes deeper in the store.
class ZarrStores {
public:
    ZarrStores(std::size_t capacity, std::chrono::seconds ttl);

    // Shared by all sessions
    static ZarrStores& Instance();

    std::shared_ptr<const ZarrStoreProbe> Probe(const std::string& path);

    // For tests
    void Clear();

private:
    using Clock = std::chrono::steady_clock;

    struct Record {
        std::int64_t directory_mtime = 0;
        Clock::time_point probed_at;
        std::shared_ptr<const ZarrStoreProbe> probe;
    };

    static std::shared_ptr<const ZarrStoreProbe> ProbeUncached(const std::string& path);

    const std::size_t _capacity;
    const std::chrono::seconds _ttl;
    std::mutex _mutex;
    std::unordered_map<std::string, Record> _records;
    std::list<std::string> _queue; // most recently used first
};

} // namespace carta

#endif // CARTA_SRC_IMAGEDATA_ZARRSTORES_H_
