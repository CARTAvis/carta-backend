/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_CARTAZARRAXES_H_
#define CARTA_SRC_IMAGEDATA_CARTAZARRAXES_H_

#include <carta-zarr/descriptor.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <casacore/casa/Arrays/IPosition.h>
#include <casacore/casa/aipstype.h>

namespace carta {

// A Zarr image's axes as CARTA's: spatial X, spatial Y, spectral, polarization. carta-zarr reads in the
// image's logical order, so that order must be CARTA's, with an optional time axis last. FromListing and
// FromImage also decide whether CARTA can display the image, returning none with the reason otherwise.
// The file list asks FromListing before offering an image and the image asks FromImage before opening,
// so the two agree.
class CartaZarrAxes {
public:
    // The axes of an image CARTA can display: spatial X, spatial Y, spectral and polarization in that order,
    // then at most a time axis of length one, with every length above zero and within CARTA's 32-bit
    // limit. Takes the listing's axes, so that images are sorted without opening them.
    static std::optional<CartaZarrAxes> FromListing(const std::vector<carta::zarr::AxisDescriptor>& axes, std::string& reason);
    // The same for an opened image, which must also have direction, spectral and polarization coordinates
    static std::optional<CartaZarrAxes> FromImage(const carta::zarr::ImageDescriptor& descriptor, std::string& reason);

    // The image's shape in CARTA's order
    const casacore::IPosition& Shape() const {
        return _shape;
    }

    // Per-axis values in the image's order, such as a chunk shape, without the time axis; missing axes are 1
    static std::vector<std::uint64_t> InCartaOrder(const std::vector<std::uint64_t>& values);

private:
    CartaZarrAxes() = default;

    casacore::IPosition _shape;
};

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
    // Why nothing is offered, if nothing is: CARTA's reason first, then carta-zarr's, then that there is no image
    std::string why_none;
};

ZarrOffer OfferedImages(const carta::zarr::DatasetDescriptor& dataset);

} // namespace carta

#endif // CARTA_SRC_IMAGEDATA_CARTAZARRAXES_H_
