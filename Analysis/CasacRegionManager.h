#ifndef CARTA_BACKEND_ANALYSIS_CASACREGIONMANAGER_H_
#define CARTA_BACKEND_ANALYSIS_CASACREGIONMANAGER_H_

#include <casacore/casa/Containers/Record.h>
#include <casacore/casa/OS/File.h>
#include <casacore/casa/namespace.h>
#include <casacore/images/Regions/ImageRegion.h>
#include <casacore/images/Regions/WCBox.h>
#include <casacore/lattices/LRegions/LCBox.h>
#include <casacore/lattices/LRegions/LCSlicer.h>
#include <casacore/measures/Measures/Stokes.h>

namespace carta {

class CasacRegionManager {
public:
    CasacRegionManager(const casacore::CoordinateSystem& csys);
    ~CasacRegionManager(){};
    casacore::Record MakeRegion(casacore::String& stokes, const casacore::String& channels, const casacore::IPosition& image_shape);

private:
    casacore::ImageRegion MakeRegion(const std::vector<casacore::Double>& box_corners, const std::vector<casacore::uInt>& channels_range,
        const std::vector<casacore::uInt>& stokes_range, casacore::IPosition image_shape) const;
    std::vector<casacore::uInt> SetSpectralRanges(casacore::String specification, casacore::uInt& selected_channels_num,
        const casacore::IPosition& image_shape = casacore::IPosition(0)) const;
    std::vector<casacore::uInt> InitSpectralRanges(casacore::uInt& selected_channels_num, const casacore::IPosition& image_shape) const;
    std::vector<casacore::uInt> SetPolarizationRanges(casacore::String& specification) const;
    casacore::Bool Supports2DBox() const;
    const casacore::CoordinateSystem& GetCoordSys() const;

    std::unique_ptr<casacore::CoordinateSystem> _csys;
};

} // namespace carta

#endif // CARTA_BACKEND_ANALYSIS_CASACREGIONMANAGER_H_
