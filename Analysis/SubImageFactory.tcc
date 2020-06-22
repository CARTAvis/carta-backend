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

// debug
//#include <components/ComponentModels/C11Timer.h>

namespace carta {

template <class T>
std::shared_ptr<casacore::SubImage<T> > SubImageFactory<T>::createSubImageRW(casacore::CountedPtr<casacore::ImageRegion>& outRegion,
    casacore::CountedPtr<casacore::ImageRegion>& outMask, casacore::ImageInterface<T>& inImage, const casacore::Record& region,
    const casacore::String& mask, casacore::LogIO* const& os, const casacore::AxesSpecifier& axesSpecifier, casacore::Bool extendMask,
    casacore::Bool preserveAxesOrder) {
    if (!mask.empty()) {
        _getMask(outMask, mask, extendMask, inImage.shape(), inImage.coordinates());
    }
    std::shared_ptr<casacore::SubImage<T> > subImage;
    // We can get away with no region processing if the region record
    // is empty and the user is not dropping degenerate axes
    if (region.nfields() == 0 && axesSpecifier.keep()) {
        subImage.reset(!outMask ? new casacore::SubImage<T>(inImage, true, axesSpecifier, preserveAxesOrder)
                                : new casacore::SubImage<T>(inImage, *outMask, true, axesSpecifier, preserveAxesOrder));
    } else {
        outRegion = casacore::ImageRegion::fromRecord(os, inImage.coordinates(), inImage.shape(), region);
        if (!outMask) {
            subImage.reset(new casacore::SubImage<T>(inImage, *outRegion, true, axesSpecifier, preserveAxesOrder));
        } else {
            // on the first pass, we need to keep all axes, the second
            // casacore::SubImage construction after this one will properly account
            // for the axes specifier
            casacore::SubImage<T> subImage0(inImage, *outMask, true, casacore::AxesSpecifier(), preserveAxesOrder);
            subImage.reset(new casacore::SubImage<T>(subImage0, *outRegion, true, axesSpecifier, preserveAxesOrder));
        }
    }
    return subImage;
}

template <class T>
std::shared_ptr<casacore::SubImage<T> > SubImageFactory<T>::createSubImageRW(casacore::ImageInterface<T>& inImage,
    const casacore::Record& region, const casacore::String& mask, casacore::LogIO* const& os, const casacore::AxesSpecifier& axesSpecifier,
    casacore::Bool extendMask, casacore::Bool preserveAxesOrder) {
    casacore::CountedPtr<casacore::ImageRegion> pRegion;
    casacore::CountedPtr<casacore::ImageRegion> pMask;
    return createSubImageRW(pRegion, pMask, inImage, region, mask, os, axesSpecifier, extendMask, preserveAxesOrder);
}

template <class T>
std::shared_ptr<const casacore::SubImage<T> > SubImageFactory<T>::createSubImageRO(casacore::CountedPtr<casacore::ImageRegion>& outRegion,
    casacore::CountedPtr<casacore::ImageRegion>& outMask, const casacore::ImageInterface<T>& inImage, const casacore::Record& region,
    const casacore::String& mask, casacore::LogIO* const& os, const casacore::AxesSpecifier& axesSpecifier, casacore::Bool extendMask,
    casacore::Bool preserveAxesOrder) {
    if (!mask.empty()) {
        _getMask(outMask, mask, extendMask, inImage.shape(), inImage.coordinates());
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
SPIIT SubImageFactory<T>::createImage(const casacore::ImageInterface<T>& image, const casacore::String& outfile,
    const casacore::Record& region, const casacore::String& mask, casacore::Bool dropDegenerateAxes, casacore::Bool overwrite,
    casacore::Bool list, casacore::Bool extendMask, casacore::Bool attachMask, const casacore::Lattice<T>* const data) {
    return createImage(
        image, outfile, region, mask, casacore::AxesSpecifier(!dropDegenerateAxes), overwrite, list, extendMask, attachMask, data);
}

template <class T>
SPIIT SubImageFactory<T>::createImage(const casacore::ImageInterface<T>& image, const casacore::String& outfile,
    const casacore::Record& region, const casacore::String& mask, const casacore::AxesSpecifier& axesSpec, casacore::Bool overwrite,
    casacore::Bool list, casacore::Bool extendMask, casacore::Bool attachMask, const casacore::Lattice<T>* const data) {
    casacore::LogIO log;
    log << casacore::LogOrigin("SubImageFactory", __func__);
    // Copy a portion of the image
    // Verify output file
    if (!overwrite && !outfile.empty()) {
        casacore::NewFile validfile;
        casacore::String errmsg;
        if (!validfile.valueOK(outfile, errmsg)) {
            // CAS-8715 users want a nicer error message in this case
            if (casacore::File(outfile).exists()) {
                errmsg = outfile + " already exists";
            }
            ThrowCc(errmsg);
        }
    }
    std::shared_ptr<const casacore::SubImage<T> > x = createSubImageRO(image, region, mask, list ? &log : 0, axesSpec, extendMask, true);
    SPIIT outImage;
    if (outfile.empty()) {
        outImage.reset(new casacore::TempImage<T>(x->shape(), x->coordinates()));
    } else {
        outImage.reset(new casacore::PagedImage<T>(x->shape(), x->coordinates(), outfile));
        if (list) {
            log << casacore::LogIO::NORMAL << "Creating image '" << outfile << "' of shape " << outImage->shape() << casacore::LogIO::POST;
        }
    }
    casacore::ImageUtilities::copyMiscellaneous(*outImage, *x);
    if (attachMask || !casa::ImageMask::isAllMaskTrue(*x)) {
        // if we don't already have a mask, but the user has specified that one needs to
        // be present, attach it. This needs to be done prior to the copyDataAndMask() call
        // because in that implementation, the image to which the mask is to be copied must
        // have already have a mask; that call does not create one if it does not exist.
        casacore::String maskName = "";
        casa::ImageMaskAttacher::makeMask(*outImage, maskName, false, true, log, list);
        if (data) {
            casa::ImageMaskHandler<T> imh(outImage);
            imh.copy(*x);
        }
    }
    if (data) {
        outImage->copyData(*data);
    } else {
        casacore::LatticeUtilities::copyDataAndMask(log, *outImage, *x);
    }
    outImage->flush();
    return outImage;
}

template <class T>
void SubImageFactory<T>::_getMask(casacore::CountedPtr<casacore::ImageRegion>& outMask, const casacore::String& mask,
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
