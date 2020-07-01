//
// From the original file: "casa/code/imageanalysis/ImageAnalysis/SubImageFactory.h"
//
#ifndef CARTA_BACKEND_ANALYSIS_SUBIMAGEFACTORY_H_
#define CARTA_BACKEND_ANALYSIS_SUBIMAGEFACTORY_H_

#include <casacore/images/Images/SubImage.h>
#include <imageanalysis/ImageTypedefs.h>

namespace carta {

template <class T>
class SubImageFactory {
public:
    SubImageFactory() = delete;

    // The const casacore::ImageInterface versions where the resulting casacore::SubImage is not
    // writable.
    static std::shared_ptr<const casacore::SubImage<T> > createSubImageRO(casacore::CountedPtr<casacore::ImageRegion>& outRegion,
        casacore::CountedPtr<casacore::ImageRegion>& outMask, const casacore::ImageInterface<T>& inImage, const casacore::Record& region,
        const casacore::String& mask, casacore::LogIO* const& os, const casacore::AxesSpecifier& axesSpecifier = casacore::AxesSpecifier(),
        casacore::Bool extendMask = false, casacore::Bool preserveAxesOrder = false);

    // variant on previous method where caller doesn't have to worry
    // about creating pointers it does not need returned.
    static std::shared_ptr<const casacore::SubImage<T> > createSubImageRO(const casacore::ImageInterface<T>& inImage,
        const casacore::Record& region, const casacore::String& mask, casacore::LogIO* const& os,
        const casacore::AxesSpecifier& axesSpecifier = casacore::AxesSpecifier(), casacore::Bool extendMask = false,
        casacore::Bool preserveAxesOrder = false);

private:
    static void GetMask(casacore::CountedPtr<casacore::ImageRegion>& outMask, const casacore::String& mask, casacore::Bool extendMask,
        const casacore::IPosition& imageShape, const casacore::CoordinateSystem& csys);
};

} // namespace carta

#include "SubImageFactory.tcc"

#endif // CARTA_BACKEND_ANALYSIS_SUBIMAGEFACTORY_H_
