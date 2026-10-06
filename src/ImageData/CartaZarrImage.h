/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_CARTAZARRIMAGE_H_
#define CARTA_SRC_IMAGEDATA_CARTAZARRIMAGE_H_

#include <optional>
#include <string>
#include <vector>

#include <carta-zarr/carta_zarr.h>
#include <casacore/images/Images/ImageInterface.h>
#include <casacore/lattices/Lattices/TiledShape.h>

#include "CartaZarrAxes.h"
#include "ZarrNotes.h"

namespace carta {

// One line of a Zarr image's storage layout, for the file info. `has_shape` says the value lists
// the image's axes in the image's order, so it can be labelled like the image shape.
struct StorageEntry {
    std::string name;
    std::string value;
    bool has_shape = false;
};

// An XRADIO sky image in CARTA's axis order: spatial X, spatial Y, spectral, polarization. A time axis
// is only accepted with a single time.
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
    void doPutSlice(const casacore::Array<float>& buffer, const casacore::IPosition& where, const casacore::IPosition& stride) override;
    const casacore::LatticeRegion* getRegionPtr() const override;
    void resize(const casacore::TiledShape& newShape) override;
    casacore::ImageInterface<float>* cloneII() const override;

    std::vector<StorageEntry> GetStorageInfo() const;
    // carta-zarr's diagnostics on the image and the notes made while describing it to casacore
    const std::vector<ZarrNote>& Notes() const {
        return _notes;
    }

private:
    void SetUpImage();
    void SetBeams();

    std::string _filename;
    std::optional<carta::zarr::Image> _zarr_image;
    // Set with _zarr_image: an image CARTA cannot display is not opened.
    std::optional<CartaZarrAxes> _axes;
    std::vector<ZarrNote> _notes;
};

} // namespace carta

#endif // CARTA_SRC_IMAGEDATA_CARTAZARRIMAGE_H_
