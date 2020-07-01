//
// From the original file: "casa/code/imageanalysis/Regions/CasacRegionManager.h"
//
#ifndef CARTA_BACKEND_ANALYSIS_CASACREGIONMANAGER_H_
#define CARTA_BACKEND_ANALYSIS_CASACREGIONMANAGER_H_

#include <casacore/images/Regions/RegionManager.h>
#include <imageanalysis/IO/RegionTextParser.h>

namespace carta {

class CasacRegionManager : public casacore::RegionManager {
public:
    const static casacore::String ALL;

    enum StokesControl { USE_FIRST_STOKES, USE_ALL_STOKES };

    CasacRegionManager(const casacore::CoordinateSystem& csys, bool verbose);

    CasacRegionManager(const CasacRegionManager&) = delete;

    ~CasacRegionManager();

    CasacRegionManager& operator=(const CasacRegionManager&) = delete;

    // convert to a record a region specified by a rectangular directional <src>box</src>,
    // <src>chans</src>, and <src>stokes</src>, or althernatively a pointer to
    // a region record. If box, chans, or stokes is not blank, <src>regionPtr</src> should
    // be null. Processing happens in the following order:
    // 1. if <src>box</src> is not empty it, along with <src>chans</src> and <src>stokes</src>
    // if specified, are used to determine the returned record. In this case <src>stokes</src>
    // is not altered.
    // 2. else if <src>regionPtr</src> is not null, it is used to determine the returned Record.
    // In this case, stokes may be modified to reflect the stokes parameters included in the
    // corresponding region.
    // 3. else if <src>regionName</src> is not empty, the region file of the same is read and
    // used to create the record, or if <src>regionName</src> is of the form "imagename:regionname" the region
    // record is read from the corresponding iamge. In this case, stokes may be modified
    // to reflect the stokes parameters included in the corresponding region.
    // 4. else <src>chans</src> and <src>stokes</src> are used to determine the region casacore::Record, or
    // if not given, the whole <src>imShape</src> is used.
    // <src>box</src> is specified in comma seperated quadruplets representing blc and trc pixel
    // locations, eg "100, 110, 200, 205". <src>stokes</src> is specified as the concatentation of
    // stokes parameters, eg "IQUV". <src>chans</src> is specified in ways supported by CASA, eg
    // "1~10" for channels 1 through 10.

    casacore::Record fromBCS(casacore::String& diagnostics, casacore::uInt& selected_channels_num, casacore::String& stokes,
        const casacore::Record* const& region_ptr, const casacore::String& region_name, const casacore::String& chans,
        const StokesControl stokes_control, const casacore::String& box, const casacore::IPosition& image_shape,
        const casacore::String& imageName = "", casacore::Bool verbose = true);

    casacore::ImageRegion fromBCS(casacore::String& diagnostics, casacore::uInt& selected_channels_num, casacore::String& stokes,
        const casacore::String& chans, StokesControl stokes_control, const casacore::String& box,
        const casacore::IPosition& image_shape) const;

    std::vector<casacore::uInt> SetSpectralRanges(casacore::String specification, casacore::uInt& selected_channels_num,
        const casacore::IPosition& image_shape = casacore::IPosition(0)) const;

private:
    std::vector<casacore::uInt> SetPolarizationRanges(casacore::String& specification) const;

    // std::vector<casacore::Double> SetBoxCorners(const casacore::String& box) const;

    casacore::ImageRegion _fromBCS(casacore::String& diagnostics, const std::vector<casacore::Double>& box_corners,
        const std::vector<casacore::uInt>& chan_end_pts, const std::vector<casacore::uInt>& pol_end_pts,
        casacore::IPosition image_shape) const;

    // does the image support the setting of a two dimensional box(es).
    casacore::Bool Supports2DBox() const;

    std::vector<casacore::uInt> InitSpectralRanges(casacore::uInt& selected_channels_num, const casacore::IPosition& image_shape) const;

    bool _verbose;
};

} // namespace carta

#endif // CARTA_BACKEND_ANALYSIS_CASACREGIONMANAGER_H_
