//
// From the original file: "casa/code/imageanalysis/ImageAnalysis/SubImageFactory.tcc"
//
#include <casacore/casa/BasicSL/String.h>
#include <casacore/images/Images/ExtendImage.h>
#include <casacore/images/Images/ImageExpr.h>
#include <casacore/images/Images/ImageUtilities.h>
#include <casacore/images/Images/PagedImage.h>
#include <casacore/images/Images/SubImage.h>
#include <casacore/images/Regions/WCLELMask.h>
#include <casacore/lattices/LRegions/LCMask.h>
#include <casacore/lattices/Lattices/LatticeUtilities.h>
#include <casacore/tables/LogTables/NewFile.h>
#include <imageanalysis/ImageAnalysis/ImageMask.h>
#include <imageanalysis/ImageAnalysis/ImageMaskAttacher.h>
#include <imageanalysis/ImageAnalysis/ImageMaskHandler.h>

namespace carta {

template <class T>
std::shared_ptr<const casacore::SubImage<T> > SubImageFactory<T>::createSubImageRO(casacore::CountedPtr<casacore::ImageRegion>& outRegion,
    casacore::CountedPtr<casacore::ImageRegion>& outMask, const casacore::ImageInterface<T>& inImage, const casacore::Record& region,
    const casacore::String& mask, casacore::LogIO* const& os, const casacore::AxesSpecifier& axesSpecifier, casacore::Bool extendMask,
    casacore::Bool preserveAxesOrder) {
    if (!mask.empty()) {
        GetMask(outMask, mask, extendMask, inImage.shape(), inImage.coordinates());
    }

    std::shared_ptr<casacore::SubImage<T> > subImage;
    // We can get away with no region processing if the region record
    // is empty and the user is not dropping degenerate axes
    if (region.nfields() == 0 && axesSpecifier.keep()) {
        subImage.reset(!outMask ? new casacore::SubImage<T>(inImage, axesSpecifier, preserveAxesOrder)
                                : new casacore::SubImage<T>(inImage, *outMask, axesSpecifier, preserveAxesOrder));
    } else {
        outRegion = casacore::ImageRegion::fromRecord(os, inImage.coordinates(), inImage.shape(), region);
        if (!outMask) {
            subImage.reset(new casacore::SubImage<T>(inImage, *outRegion, axesSpecifier, preserveAxesOrder));
        } else {
            // on the first pass, we need to keep all axes, the second
            // casacore::SubImage construction after this one will properly account
            // for the axes specifier
            casacore::SubImage<T> subImage0(inImage, *outMask, casacore::AxesSpecifier(), preserveAxesOrder);
            subImage.reset(new casacore::SubImage<T>(subImage0, *outRegion, axesSpecifier, preserveAxesOrder));
        }
    }

    return subImage;
}

template <class T>
std::shared_ptr<const casacore::SubImage<T> > SubImageFactory<T>::createSubImageRO(const casacore::ImageInterface<T>& inImage,
    const casacore::Record& region, const casacore::String& mask, casacore::LogIO* const& os, const casacore::AxesSpecifier& axesSpecifier,
    casacore::Bool extendMask, casacore::Bool preserveAxesOrder) {
    casacore::CountedPtr<casacore::ImageRegion> pRegion;
    casacore::CountedPtr<casacore::ImageRegion> pMask;

    return createSubImageRO(pRegion, pMask, inImage, region, mask, os, axesSpecifier, extendMask, preserveAxesOrder);
}

template <class T>
void SubImageFactory<T>::GetMask(casacore::CountedPtr<casacore::ImageRegion>& outMask, const casacore::String& mask,
    casacore::Bool extendMask, const casacore::IPosition& imageShape, const casacore::CoordinateSystem& csys) {
    casacore::String mymask = mask;
    for (casacore::uInt i = 0; i < 2; i++) {
        try {
            outMask = casacore::ImageRegion::fromLatticeExpression(mymask);
            break;
        } catch (const casacore::AipsError& x) {
            if (i == 0) {
                // not an LEL expression, perhaps it's a clean mask image name
                mymask += ">=0.5";
                continue;
            }
            ThrowCc("Input mask specification is incorrect: " + x.getMesg());
        }
    }

    if (outMask && outMask->asWCRegion().type() == "WCLELMask") {
        const casacore::ImageExpr<casacore::Bool>* myExpression =
            dynamic_cast<const casacore::WCLELMask*>(outMask->asWCRegionPtr())->getImageExpr();

        if (myExpression && !myExpression->shape().isEqual(imageShape)) {
            if (!extendMask) {
                ostringstream os;
                os << "The input image shape (" << imageShape << ") and mask shape (" << myExpression->shape()
                   << ") are different, and it was specified "
                      "that the mask should not be extended, so the mask cannot be applied to the "
                      "(sub)image. Specifying that the mask should be extended may resolve the issue";
                ThrowCc(os.str());
            }

            try {
                casacore::ExtendImage<casacore::Bool> exIm(*myExpression, imageShape, csys);
                outMask = new casacore::ImageRegion(casacore::LCMask(exIm));
            } catch (const casacore::AipsError& x) {
                ThrowCc("Unable to extend mask: " + x.getMesg());
            }
        }
    }
}

} // namespace carta
