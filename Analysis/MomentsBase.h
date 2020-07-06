//
// Modify from the original file: "casa/code/imageanalysis/ImageAnalysis/MomentsBase.h"
//
#ifndef CARTA_BACKEND_ANALYSIS_MOMENTSBASE_H_
#define CARTA_BACKEND_ANALYSIS_MOMENTSBASE_H_

#include <casacore/casa/Arrays/Vector.h>
#include <casacore/casa/BasicSL/String.h>
#include <casacore/casa/Containers/Record.h>
#include <casacore/casa/Containers/RecordFieldId.h>
#include <casacore/casa/Exceptions/Error.h>
#include <casacore/casa/Logging/LogIO.h>
#include <casacore/casa/Quanta/Quantum.h>
#include <casacore/casa/Quanta/Unit.h>
#include <casacore/casa/Quanta/UnitMap.h>
#include <casacore/casa/Utilities/LinearSearch.h>
#include <casacore/casa/aips.h>
#include <casacore/casa/iomanip.h>
#include <casacore/casa/iosfwd.h>
#include <casacore/casa/sstream.h>
#include <casacore/coordinates/Coordinates/CoordinateSystem.h>
#include <casacore/lattices/LatticeMath/LatticeStatsBase.h>
#include <casacore/lattices/LatticeMath/LineCollapser.h>
#include <casacore/measures/Measures/MDoppler.h>
#include <casacore/tables/LogTables/NewFile.h>

namespace carta {

template <class T>
class MomentCalcBase;

// <summary>
// This class is a base class for generating moments from an image or a spectral
// data.
// </summary>

// <use visibility=export>

// <reviewed reviewer="" date="yyyy/mm/dd" tests="" demos="">
// </reviewed>

// <prerequisite>
//   <li> <linkto class="ImageMoments">ImageMoments</linkto>
//   <li> <linkto class="MSMoments">MSMoments</linkto>
//   <li> <linkto class="casacore::LatticeApply">casacore::LatticeApply</linkto>
//   <li> <linkto class="MomentCalcBase">MomentCalcBase</linkto>
// </prerequisite>

// <etymology>
//   This class is an abstract class to compute moments from images or spectral
//   data.
// </etymology>

// <synopsis>
//  The primary goal of MSMoments, ImageMoments, and MSMoments are to help
//  spectral-line astronomers analyze their multi-dimensional images or
//  spectral data (in the form of casacore::MeasurementSet) by generating
//  moments of a specified axis.  ImageMoments is a specialized class to
//  generate moments from images, while MSMoments is designed properly for
//  casacore::MeasurementSet input. MSMoments class is an abstract class that is
//  inherited by the above two concrete classes. MomentsBase provides interface
//  layer to the MomentCalculators so that functionalities in MomentCalculators
//  can be shared with ImageMoments and MSMoments. The word "moment" is used
//  loosely here.  It refers to collapsing an axis to one pixel and putting the
//  value of that pixel (for all of the other non-collapsed axes) to something
//  computed from the data values along the moment axis.  For example, take an
//  RA-DEC-Velocity cube, collapse the velocity axis by computing the mean
//  intensity at each RA-DEC pixel.  This class and its inheritances offer many
//  different moments and a variety of interactive and automatic ways to compute
//  them.
//

// <motivation>
//  MSMoments is defined so that moments can be generated from both images
//  and spectral data (in the form of casacore::MeasurementSet).
// </motivation>

template <class T>
class MomentsBase {
public:
    // Note that if I don't put MomentCalcBase as a forward declaration
    // and use instead  "friend class MomentCalcBase<T>"  The gnu compiler
    // fails with a typedef problem.  But I can't solve it with say
    // <src>typedef MomentCalcBase<T> gpp_type;</src>  because you can only do a
    // typedef with an actual type, not a <tt>T</tt> !
    friend class MomentCalcBase<T>;

    // The <src>enum MethodTypes</src> is provided for use with the
    // <src>setWinFitMethod</src> function.  It gives the allowed moment
    // methods which are available with this function.  The use of these
    // methods is described further with the description of this function
    // as well as in the general documentation earlier.
    enum MethodTypes {
        // Invokes the spectral windowing method
        WINDOW,
        // Invokes Gaussian fitting
        FIT,
        NMETHODS
    };

    // This <src>enum MomentTypes</src> is provided for use with the
    // <src>setMoments</src> function.  It gives the allowed moment
    // types that you can ask for.

    enum MomentTypes {
        // The average intensity
        AVERAGE,
        // The integrated intensity
        INTEGRATED,
        // The intensity weighted mean coordinate (usually velocity)
        WEIGHTED_MEAN_COORDINATE,
        // The intensity weighted coordinate (usually velocity) dispersion
        WEIGHTED_DISPERSION_COORDINATE,
        // The median intensity
        MEDIAN,
        // The median coordinate (usually velocity). Treat the spectrum as
        // a probability distribution, generate the cumulative distribution,
        // and find the coordinate corresponding to the 50% value.
        MEDIAN_COORDINATE,
        // The standard deviation about the mean of the intensity
        STANDARD_DEVIATION,
        // The rms of the intensity
        RMS,
        // The absolute mean deviation of the intensity
        ABS_MEAN_DEVIATION,
        // The maximum value of the intensity
        MAXIMUM,
        // The coordinate (usually velocity) of the maximum value of the intensity
        MAXIMUM_COORDINATE,
        // The minimum value of the intensity
        MINIMUM,
        // The coordinate (usually velocity) of the minimum value of the intensity
        MINIMUM_COORDINATE,
        // Total number
        NMOMENTS,
        // Default value is the integrated intensity
        DEFAULT = INTEGRATED
    };

    // Destructor
    virtual ~MomentsBase();

    // Set the desired moments via an <src>casacore::Int</src> array.  Each
    // <src>casacore::Int</src> specifies a different moment; the allowed values
    // and their meanings are given by the <src>enum MomentTypes</src>.   A return
    // value of <src>false</src> indicates you asked for an out of range moment.
    // If you don't call this function, the default state of the class is to
    // request the integrated intensity.
    casacore::Bool setMoments(const casacore::Vector<casacore::Int>& moments);

    // Set the moment axis (0 relative).  A return value of <src>false</src>
    // indicates that the axis was not contained in the image. If you don't
    // call this function, the default state of the class is to set the
    // moment axis to the spectral axis if it can find one.  Otherwise
    // an error will result.
    virtual casacore::Bool setMomentAxis(casacore::Int) = 0;

    // The method by which you compute the moments is specified by calling
    // (or not calling) the <src>setWinFitMethod</src> and
    // <src>setSmoothMethod</src> functions.  The default state of the class
    // is to compute directly on all (or some according to
    // <src>setInExClude</src>) of the pixels in the spectrum.  Calling these
    // functions modifies the computational state to something more complicated.
    //
    // The <src>setWinMethod</src> function requires an <src>casacore::Int</src>
    // array as its argument.  Each <src>casacore::Int</src> specifies a different
    // method that you can invoke (either separately or in combination).  The
    // allowed values and their meanings are given by the
    // <src>enum MethodTypes</src>.
    //
    // Both the windowing and fitting methods have interactive modes. The
    // windowing method also has a fitting flavour, so if you set both
    // MomentsBase::WINDOW and MomentsBase::FIT, you would be invoking the
    // windowing method but determining the window by fitting Gaussians
    // automatically (as MomentsBase::INTERACTIVE) was not given.
    //
    // If you don't call this function, then neither the windowing nor fitting
    // methods are invoked.  A return value of <src>false</src> indicates
    // that you asked for an illegal method.
    casacore::Bool setWinFitMethod(const casacore::Vector<casacore::Int>& method);

    // This function invokes smoothing of the input image.  Give
    // <src>casacore::Int</src> arrays for the axes (0 relative) to be smoothed
    // and the smoothing kernel types (use the <src>enum KernelTypes</src>) for
    // each axis.  Give a <src>casacore::Double</src> array for the widths (full
    // width for BOXCAR and full width at half maximum for GAUSSIAN) in pixels of
    // the smoothing kernels for each axis.  For HANNING smoothing, you always get
    // the quarter-half-quarter kernel (no matter what you might ask for).  A
    // return value of <src>false</src> indicates that you have given an
    // inconsistent or invalid set of smoothing parameters.  If you don't call
    // this function the default state of the class is to do no smoothing.  The
    // kernel types are specified with the casacore::VectorKernel::KernelTypes
    // enum <group>
    virtual casacore::Bool setSmoothMethod(const casacore::Vector<casacore::Int>&, const casacore::Vector<casacore::Int>&,
        const casacore::Vector<casacore::Quantum<casacore::Double>>&) = 0;

    casacore::Bool setSmoothMethod(const casacore::Vector<casacore::Int>& smoothAxes, const casacore::Vector<casacore::Int>& kernelTypes,
        const casacore::Vector<casacore::Double>& kernelWidths);
    // </group>

    // You may specify a pixel intensity range as either one for which
    // all pixels in that range are included in the moment calculation,
    // or one for which all pixels in that range are excluded from the moment
    // calculations.  One or the other of <src>include</src> and
    // <src>exclude</src> must therefore be a zero length vector if you call this
    // function. A return value of <src>false</src> indicates that you have given
    // both an <src>include</src> and an <src>exclude</src> range.  If you don't
    // call this function, the default state of the class is to include all
    // pixels.
    void setInExCludeRange(const casacore::Vector<T>& include, const casacore::Vector<T>& exclude);

    // This function is used to help assess whether a spectrum along the moment
    // axis is all noise or not.  If it is all noise, there is not a lot of point
    // to trying to computing the moment.
    // <src>peakSNR</src> is the signal-to-noise ratio of the peak value in the
    // spectrum below which the spectrum is considered pure noise.
    // <src>stdDeviation</src> is the standard deviation of the noise for the
    // input image.
    //
    // Default values for one or the other parameter are indicated by giving zero.
    //
    // The default state of the class then is to set <src>peakSNR=3</src>
    // and/or to work out the noise level from a Gaussian fit to a histogram
    // (above 25%) of the entire image (it is very hard to get an accurate
    // estimate of the noise a single spectrum).
    void setSnr(const T& peakSNR, const T& stdDeviation);

    // This is the output file name for the smoothed image.   It can be useful
    // to have access this to this image when trying to get the pixel
    // <src>include</src> or <src>exclude</src> range correct for the smooth-clip
    // method.  The default state of the class is to not output the smoothed
    // image.
    casacore::Bool setSmoothOutName(const casacore::String& smOut);

    // Set Velocity type.  This is used for moments for which the moment axis is
    // a spectral axis for which the coordinate is traditionally presented in
    // km/s   You can select the velocity definition. The default state of the
    // class is to use the radio definition.
    void setVelocityType(casacore::MDoppler::Types type);

    // Reset argument error condition.  If you specify invalid arguments to
    // one of the above functions, an internal flag will be set which will
    // prevent the <src>createMoments</src> function, which is defined in its
    // inheritances, from doing anything (should you have chosen to igmore the
    // Boolean return values of the functions). This function allows you to reset
    // that internal state to good.
    void resetError() {
        goodParameterStatus_p = true;
        error_p = "";
    };

    // Recover last error message
    casacore::String errorMessage() const {
        return error_p;
    };

    // Get CoordinateSystem
    virtual const casacore::CoordinateSystem& coordinates() = 0;

    // Helper function to convert a string containing a list of desired methods to
    // the correct <src>casacore::Vector<casacore::Int></src> required for the
    // <src>setWinFitMethod</src> function. This may be usful if your user
    // interface involves strings rather than integers. A new value is added to
    // the output vector (which is resized appropriately) if any of the substrings
    // "window", "fit" or "interactive" (actually "win", "box" and "inter" will
    // do) is present.
    static casacore::Vector<casacore::Int> toMethodTypes(const casacore::String& methods);

    virtual casacore::IPosition getShape() const = 0;

    casacore::Bool shouldConvertToVelocity() const {
        return convertToVelocity_p;
    }

protected:
    // Constructor takes an image and a <src>casacore::LogIO</src> object for
    // logging purposes. You specify whether output images are  automatically
    // overwritten if pre-existing, or whether an intercative choice dialog widget
    // appears (overWriteOutput=F) You may also determine whether a progress meter
    // is displayed or not.
    MomentsBase(casacore::LogIO& os, casacore::Bool overWriteOutput = false, casacore::Bool showProgress = true);

    casacore::LogIO os_p;
    casacore::Bool showProgress_p;
    casacore::Int momentAxisDefault_p{-10};
    T peakSNR_p{T(3)};
    T stdDeviation_p{T(0)};
    T yMin_p{T(0)};
    T yMax_p{T(0)};
    casacore::String out_p;
    casacore::String smoothOut_p{};
    casacore::Bool goodParameterStatus_p{true};
    casacore::Bool doWindow_p{false};
    casacore::Bool doFit_p{false};
    casacore::Bool doSmooth_p{false};
    casacore::Bool noInclude_p{true};
    casacore::Bool noExclude_p{true};
    casacore::Bool fixedYLimits_p{false};

    casacore::Int momentAxis_p{momentAxisDefault_p};
    casacore::Int worldMomentAxis_p;
    casacore::Vector<casacore::Int> kernelTypes_p{};
    casacore::Vector<casacore::Quantity> kernelWidths_p{};
    casacore::Vector<casacore::Int> moments_p{1, INTEGRATED};
    casacore::Vector<T> selectRange_p{};
    casacore::Vector<casacore::Int> smoothAxes_p{};
    casacore::Bool overWriteOutput_p;
    casacore::String error_p{};
    casacore::Bool convertToVelocity_p{false};
    casacore::MDoppler::Types velocityType_p{casacore::MDoppler::RADIO};

    // Check that the combination of methods that the user has requested is valid
    // casacore::List a handy dandy table if not.
    void _checkMethod();

    // Take the user's data inclusion and exclusion data ranges and
    // generate the range and Booleans to say what sort it is
    void _setIncludeExclude(casacore::Vector<T>& range, casacore::Bool& noInclude, casacore::Bool& noExclude,
        const casacore::Vector<T>& include, const casacore::Vector<T>& exclude);

    // Set the output image suffixes and units
    casacore::Bool _setOutThings(casacore::String& suffix, casacore::Unit& momentUnits, const casacore::Unit& imageUnits,
        const casacore::String& momentAxisUnits, const casacore::Int moment, casacore::Bool convertToVelocity);

    // Make output Coordinates
    casacore::CoordinateSystem _makeOutputCoordinates(casacore::IPosition& outShape, const casacore::CoordinateSystem& cSysIn,
        const casacore::IPosition& inShape, casacore::Int momentAxis, casacore::Bool removeAxis);
};

} // namespace carta

#include "MomentsBase.tcc"

#endif // CARTA_BACKEND_ANALYSIS_MOMENTSBASE_H_
