//
// From the original file: "casa/code/imageanalysis/Regions/CasacRegionManager.cc"
//
#include "CasacRegionManager.h"

#include <casacore/casa/Containers/Record.h>
#include <casacore/casa/OS/File.h>
#include <casacore/casa/namespace.h>
#include <casacore/images/Images/SubImage.h>
#include <casacore/images/Images/TempImage.h>
#include <casacore/images/Regions/ImageRegion.h>
#include <casacore/images/Regions/WCBox.h>
#include <casacore/lattices/LRegions/LCBox.h>
#include <casacore/lattices/LRegions/LCSlicer.h>
#include <casacore/measures/Measures/Stokes.h>
#include <casacore/tables/Tables/TableRecord.h>
#include <imageanalysis/Annotations/AnnRegion.h>
#include <imageanalysis/Annotations/RegionTextList.h>
#include <imageanalysis/IO/ParameterParser.h>
#include <imageanalysis/ImageAnalysis/ImageMetaData.h>
#include <imageanalysis/ImageAnalysis/SubImageFactory.h>

#include <memory>

using namespace casacore;

namespace carta { //# name space carta begins

const String CasacRegionManager::ALL = "ALL";

CasacRegionManager::CasacRegionManager() : RegionManager() {}

CasacRegionManager::CasacRegionManager(const CoordinateSystem& csys) : RegionManager(csys) {}

CasacRegionManager::~CasacRegionManager() {}

vector<uInt> CasacRegionManager::_setPolarizationRanges(
    String& specification, const String& firstStokes, const uInt nStokes, const StokesControl stokesControl) const {
    LogOrigin origin("CasacRegionManager", __func__);
    // const LogIO *myLog = _getLog();
    *_getLog() << origin;
    vector<uInt> ranges(0);
    CoordinateSystem csys = getcoordsys();
    if (!csys.hasPolarizationCoordinate()) {
        return ranges;
    }
    specification.trim();
    specification.ltrim('[');
    specification.rtrim(']');

    specification.upcase();
    if (specification == ALL) {
        ranges.push_back(0);
        ranges.push_back(nStokes - 1);
        return ranges;
    }
    if (specification.empty()) {
        // ranges.resize(2);
        // ranges[0] = 0;
        ranges.push_back(0);
        switch (stokesControl) {
            case USE_FIRST_STOKES:
                ranges.push_back(0);
                specification = firstStokes;
                break;
            case USE_ALL_STOKES:
                ranges.push_back(nStokes - 1);
                specification = ALL;
                break;
            default:
                // bug if we get here
                ThrowCc("Logic error, unhandled stokes control");
                break;
        };
        return ranges;
    }
    // First split on commas and semi-colons.
    // in the past for polarization specification.

    Vector<String> parts = stringToVector(specification, Regex("[,;]"));
    Vector<String> polNames = Stokes::allNames(false);
    uInt nNames = polNames.size();
    Vector<uInt> nameLengths(nNames);
    for (uInt i = 0; i < nNames; i++) {
        nameLengths[i] = polNames[i].length();
    }
    uInt* lengthData = nameLengths.data();

    Vector<uInt> idx(nNames);
    Sort sorter;
    sorter.sortKey(lengthData, TpUInt, 0, Sort::Descending);
    sorter.sort(idx, nNames);

    Vector<String> sortedNames(nNames);
    for (uInt i = 0; i < nNames; i++) {
        sortedNames[i] = polNames[idx[i]];
        sortedNames[i].upcase();
    }
    for (uInt i = 0; i < parts.size(); i++) {
        String part = parts[i];
        part.trim();
        Vector<String>::iterator iter = sortedNames.begin();
        while (iter != sortedNames.end() && !part.empty()) {
            if (part.startsWith(*iter)) {
                Int stokesPix = csys.stokesPixelNumber(*iter);
                if (stokesPix >= int(nStokes)) {
                    *_getLog() << "Polarization " << *iter << " specified in " << parts[i] << " does not exist in the specified "
                               << "coordinate system for the specified number of "
                               << "polarization parameters" << LogIO::EXCEPTION;
                }
                ranges.push_back(stokesPix);
                ranges.push_back(stokesPix);
                // consume the string
                part = part.substr(iter->length());
                if (!part.empty()) {
                    // reset the iterator to start over at the beginning of the list for
                    // the next specified polarization
                    iter = sortedNames.begin();
                }
            } else {
                iter++;
            }
        }
        if (!part.empty()) {
            *_getLog() << "(Sub)String " << part << " in stokes specification part " << parts[i] << " does not match a known polarization."
                       << LogIO::EXCEPTION;
        }
    }
    uInt nSel;
    return casa::ParameterParser::consolidateAndOrderRanges(nSel, ranges);
}

Bool CasacRegionManager::_supports2DBox(Bool except) const {
    Bool ok = true;
    const CoordinateSystem& csys = getcoordsys();
    Vector<Int> axes;
    if (csys.hasDirectionCoordinate()) {
        axes = csys.directionAxesNumbers();
    } else if (csys.hasLinearCoordinate()) {
        axes = csys.linearAxesNumbers();
    } else {
        ok = false;
    }

    if (ok) {
        uInt nGood = 0;
        for (uInt i = 0; i < axes.size(); i++) {
            if (axes[i] >= 0) {
                nGood++;
            }
        }
        if (nGood != 2) {
            ok = false;
        }
    }

    if (except && !ok) {
        *_getLog() << LogOrigin("CasacRegionManager", __func__) << "This image does not have a 2-D direction or linear coordinate";
    }

    return ok;
}

vector<Double> CasacRegionManager::_setBoxCorners(const String& box) const {
    if (!box.empty()) {
        _supports2DBox(true);
    }
    Vector<String> boxParts = stringToVector(box);
    AlwaysAssert(boxParts.size() % 4 == 0, AipsError);
    vector<Double> corners(boxParts.size());
    for (uInt i = 0; i < boxParts.size() / 4; i++) {
        uInt index = 4 * i;
        for (uInt j = 0; j < 4; j++) {
            boxParts[index + j].trim();
            if (!boxParts[index + j].matches(RXdouble)) {
                *_getLog() << "Box spec contains non numeric characters and so is invalid" << LogIO::EXCEPTION;
            }
            corners[index + j] = String::toDouble(boxParts[index + j]);
        }
    }
    return corners;
}

Record CasacRegionManager::fromBCS(String& diagnostics, uInt& nSelectedChannels, String& stokes, const Record* const& regionPtr,
    const String& regionName, const String& chans, const StokesControl stokesControl, const String& box, const IPosition& imShape,
    const String& imageName, Bool verbose) {
    LogOrigin origin("CasacRegionManager", __func__);

    Record regionRecord;

    if (!box.empty()) {
        // Apply box settings
        ThrowIf(regionPtr != nullptr, "box and regionPtr cannot be simultaneously specified");
        ThrowIf(box.freq(",") % 4 != 3, "box not specified correctly");

        if (regionName.empty()) {
            // Region is empty but box is not empty
            regionRecord = fromBCS(diagnostics, nSelectedChannels, stokes, chans, stokesControl, box, imShape).toRecord("");
            if (verbose) {
                *_getLog() << origin;
                *_getLog() << LogIO::NORMAL << "Using specified box(es) " << box << LogIO::POST;
            }
        } else {
            // Region and box are not empty
            auto corners = stringToVector(box, ',');
            auto iter = corners.begin();
            auto end = corners.end();

            String crtfBoxString = "box[[";
            auto count = 0;
            for (; iter != end; ++iter, ++count) {
                iter->trim();

                if (count > 0 && count % 4 == 0) {
                    crtfBoxString += "\nbox[[";
                }

                crtfBoxString += *iter + "pix";

                if (count % 2 == 0) {
                    crtfBoxString += ",";
                } else {
                    // close pixel pair
                    crtfBoxString += "]";
                    if (count - 1 % 4 == 0) {
                        // first pixel pair done, start second pixel pair
                        crtfBoxString += ",[";
                    } else {
                        // second pixel pair done, close box
                        crtfBoxString += "]";
                    }
                }
            }

            _setRegion(regionRecord, diagnostics, regionName, imShape, imageName, crtfBoxString, chans, stokes, verbose);
        }

    } else if (regionPtr) {
        // Apply region record settings
        ThrowIf(!(regionName.empty() && chans.empty() && stokes.empty()),
            "regionPtr and regionName, chans, and/or stokes cannot "
            "be simultaneously specified");

        _setRegion(regionRecord, diagnostics, regionPtr);
        stokes = _stokesFromRecord(regionRecord, stokesControl, imShape);

    } else if (!regionName.empty()) {
        // Apply region record settings with respect to the region name
        _setRegion(regionRecord, diagnostics, regionName, imShape, imageName, "", chans, stokes, verbose);
        if (verbose) {
            *_getLog() << origin;
            *_getLog() << LogIO::NORMAL << diagnostics << LogIO::POST;
        }
        stokes = _stokesFromRecord(regionRecord, stokesControl, imShape);

    } else {
        // With no box settings and no region record settings
        vector<uInt> chanEndPts, polEndPts;
        regionRecord = fromBCS(diagnostics, nSelectedChannels, stokes, chans, stokesControl, box, imShape).toRecord("");
        if (verbose) {
            *_getLog() << origin;
            *_getLog() << LogIO::NORMAL << "No directional region specified. Using full positional plane." << LogIO::POST;
        }

        const CoordinateSystem& csys = getcoordsys();
        if (csys.hasSpectralAxis()) {
            if (verbose) {
                if (chans.empty()) {
                    *_getLog() << LogIO::NORMAL << "Using all spectral channels." << LogIO::POST;
                } else {
                    *_getLog() << LogIO::NORMAL << "Using channel range(s) " << _pairsToString(chanEndPts) << LogIO::POST;
                }
            }
        }

        if (csys.hasPolarizationCoordinate() && verbose) {
            if (stokes.empty()) {
                switch (stokesControl) {
                    case USE_ALL_STOKES:
                        *_getLog() << LogIO::NORMAL << "Using all polarizations " << LogIO::POST;
                        break;
                    case USE_FIRST_STOKES:
                        *_getLog() << LogIO::NORMAL << "polarization " << csys.stokesAtPixel(0) << LogIO::POST;
                        break;
                    default:
                        break;
                }
            } else {
                *_getLog() << LogIO::NORMAL << "Using polarizations " << stokes << LogIO::POST;
            }
        }
    }

    return regionRecord;
}

Record CasacRegionManager::regionFromString(
    const CoordinateSystem& csys, const String& regionStr, const String& imageName, const IPosition& imShape, Bool verbose) {
    CasacRegionManager mgr(csys);
    Record reg;
    String diag;
    mgr._setRegion(reg, diag, regionStr, imShape, imageName, "", "", "", verbose);
    return reg;
}

void CasacRegionManager::_setRegion(Record& regionRecord, String& diagnostics, const Record* regionPtr) {
    // region record pointer provided
    regionRecord = *(regionPtr);
    // set stokes from the region record
    diagnostics = "used provided region record";
}

void CasacRegionManager::_setRegion(Record& regionRecord, String& diagnostics, const String& regionName, const IPosition& imShape,
    const String& imageName, const String& prependBox, const String& globalOverrideChans, const String& globalStokesOverride,
    Bool verbose) {
    if (regionName.empty() && imageName.empty()) {
        regionRecord = Record();
        diagnostics = "No region string";
        return;
    }

    // region name provided
    const static Regex image("(.*)+:(.*)+");
    const static Regex regionText("^[[:space:]]*[[:alpha:]]+[[:space:]]*\\[(.*)+,(.*)+\\]");

    File myFile(regionName);

    const CoordinateSystem csys = getcoordsys();

    if (myFile.exists()) {
        ThrowIf(!myFile.isReadable(), "File " + regionName + " exists but is not readable.");

        std::unique_ptr<Record> rec;
        try {
            rec.reset(readImageFile(regionName, ""));
        } catch (const AipsError& x) {
        }

        if (rec) {
            ThrowIf(!globalOverrideChans.empty() || !globalStokesOverride.empty() || !prependBox.empty(),
                "a binary region file and any of box, chans and/or stokes cannot "
                "be specified simultaneously");
            regionRecord = *rec;
            diagnostics = "Region read from binary region file " + regionName;
            return;
        }

        try {
            // CRTF file attempt
            casa::RegionTextList annList(regionName, csys, imShape, prependBox, globalOverrideChans, globalStokesOverride);
            regionRecord = annList.regionAsRecord();
            diagnostics = "Region read from CRTF file " + regionName;
        } catch (const AipsError& x) {
            ThrowCc(regionName +
                    " is neither a valid binary region file, "
                    "nor a valid region text file.");
        }

    } else if (regionName.contains(regionText)) {
        // region spec is raw CASA region plaintext
        try {
            casa::RegionTextList annList(csys, regionName, imShape, prependBox, globalOverrideChans, globalStokesOverride, verbose);
            regionRecord = annList.regionAsRecord();
            diagnostics = "Region read from text string " + regionName;
        } catch (const AipsError& x) {
            ThrowCc(x.getMesg());
        }

    } else if (regionName.matches(image) || !imageName.empty()) {
        ImageRegion imRegion;
        String imagename, region;
        if (regionName.matches(image)) {
            String res[2];
            casacore::split(regionName, res, 2, ":");
            imagename = res[0];
            region = res[1];
        } else {
            // imageName is not empty if we get here
            imagename = imageName;
            region = regionName;
        }

        try {
            Record* myRec = tableToRecord(imagename, region);
            ThrowIf(!globalOverrideChans.empty() || !globalStokesOverride.empty() || !prependBox.empty(),
                "a region-in-image and any of box, chans and/or stokes cannot "
                "be specified simultaneously");

            if (Table::isReadable(imagename)) {
                ThrowIf(myRec == 0, "Region " + region + " not found in image " + imagename);
                regionRecord = *myRec;
                diagnostics = "Used region " + region + " from image " + imagename + " table description";
            } else {
                *_getLog() << "Cannot read image " << imagename << " to get region " << region << LogIO::EXCEPTION;
            }

        } catch (const AipsError&) {
            ThrowCc("Unable to open region file or region table description " + region + " in image " + imagename);
        }

    } else {
        ostringstream oss;
        oss << "Unable to open region file or region table description " << regionName << "." << endl
            << "If it is supposed to be a text string its format is incorrect";
        ThrowCc(oss.str());
    }
}

ImageRegion CasacRegionManager::fromBCS(String& diagnostics, uInt& nSelectedChannels, String& stokes, const String& chans,
    const StokesControl stokesControl, const String& box, const IPosition& imShape) const {
    vector<uInt> chanEndPts = setSpectralRanges(chans, nSelectedChannels, imShape);

    const CoordinateSystem& csys = getcoordsys();
    Int polAxisNumber = csys.polarizationAxisNumber();
    uInt nTotalPolarizations = polAxisNumber >= 0 ? imShape[polAxisNumber] : 0;
    String firstStokes = polAxisNumber >= 0 ? csys.stokesAtPixel(0) : "";
    vector<uInt> polEndPts = _setPolarizationRanges(stokes, firstStokes, nTotalPolarizations, stokesControl);

    vector<Double> boxCorners;
    if (box.empty()) {
        if (_supports2DBox(false)) {
            if (csys.hasDirectionCoordinate() || csys.hasLinearCoordinate()) {
                Vector<Int> dirAxesNumbers;
                if (csys.hasDirectionCoordinate()) {
                    dirAxesNumbers = csys.directionAxesNumbers();
                } else {
                    dirAxesNumbers = csys.linearAxesNumbers();
                }

                Vector<Int> dirShape(2);
                dirShape[0] = imShape[dirAxesNumbers[0]];
                dirShape[1] = imShape[dirAxesNumbers[1]];

                boxCorners.resize(4);
                boxCorners[0] = 0;               // bl: x
                boxCorners[1] = 0;               // bl: y
                boxCorners[2] = dirShape[0] - 1; // tr: x
                boxCorners[3] = dirShape[1] - 1; // tr: y
            }
        }
    } else {
        boxCorners = _setBoxCorners(box);
    }

    return _fromBCS(diagnostics, boxCorners, chanEndPts, polEndPts, imShape);
}

ImageRegion CasacRegionManager::_fromBCS(String& diagnostics, const vector<Double>& boxCorners, const vector<uInt>& chanEndPts,
    const vector<uInt>& polEndPts, const IPosition imShape) const {
    LogOrigin origin("CasacRegionManager", __func__);
    *_getLog() << origin;

    Vector<Double> blc(imShape.nelements(), 0);
    Vector<Double> trc(imShape.nelements(), 0);

    const CoordinateSystem csys = getcoordsys();
    Vector<Int> directionAxisNumbers = csys.directionAxesNumbers();
    vector<Int> linearAxisNumbers = csys.linearAxesNumbers().tovector();

    // Stupidly, sometimes the values returned by linearAxesNumbers can be less than 0
    // This needs to be fixed in the implementation of that method
    vector<Int>::iterator iter = linearAxisNumbers.begin();
    vector<Int>::iterator end = linearAxisNumbers.end();
    while (iter != end) {
        if (*iter < 0) {
            iter = linearAxisNumbers.erase(iter);
        }
        ++iter;
    }

    Int spectralAxisNumber = csys.spectralAxisNumber();
    Int polarizationAxisNumber = csys.polarizationAxisNumber();

    Vector<Double> xCorners(boxCorners.size() / 2);
    Vector<Double> yCorners(xCorners.size());

    for (uInt i = 0; i < xCorners.size(); i++) {
        Double x = boxCorners[2 * i];
        Double y = boxCorners[2 * i + 1];

        if (x < 0 || y < 0) {
            *_getLog() << "blc in box spec is less than 0" << LogIO::EXCEPTION;
        }

        if (csys.hasDirectionCoordinate()) {
            if (x >= imShape[directionAxisNumbers[0]] || y >= imShape[directionAxisNumbers[1]]) {
                *_getLog() << "dAxisNum0=" << directionAxisNumbers[0] << " dAxisNum1=" << directionAxisNumbers[1];
                *_getLog() << "x=" << x << " imShape[0]=" << imShape[directionAxisNumbers[0]] << " y=" << y
                           << " imShape[1]=" << imShape[directionAxisNumbers[1]] << LogIO::POST;
                *_getLog() << "trc in box spec is greater than or equal to number "
                           << "of direction coordinate pixels in the image" << LogIO::EXCEPTION;
            }
        } else if (csys.hasLinearCoordinate() && (x >= imShape[linearAxisNumbers[0]] || y >= imShape[linearAxisNumbers[1]])) {
            *_getLog() << "trc in box spec is greater than or equal to number "
                       << "of linear coordinate pixels in the image" << LogIO::EXCEPTION;
        }

        xCorners[i] = x;
        yCorners[i] = y;
    }

    Vector<Double> polEndPtsDouble(polEndPts.size());
    for (uInt i = 0; i < polEndPts.size(); ++i) {
        polEndPtsDouble[i] = (Double)polEndPts[i];
    }

    Bool csysSupports2DBox = _supports2DBox(false);
    uInt nRegions = 1;
    if (csysSupports2DBox) {
        if (csys.hasDirectionCoordinate()) {
            nRegions *= boxCorners.size() / 4;
        }
        if (csys.hasLinearCoordinate()) {
            nRegions *= boxCorners.size() / 4;
        }
    }
    if (csys.hasPolarizationCoordinate()) {
        nRegions *= polEndPts.size() / 2;
    }
    if (csys.hasSpectralAxis()) {
        nRegions *= chanEndPts.size() / 2;
    }

    Vector<Double> extXCorners(2 * nRegions, 0);
    Vector<Double> extYCorners(2 * nRegions, 0);
    Vector<Double> extPolEndPts(2 * nRegions, 0);
    Vector<Double> extChanEndPts(2 * nRegions, 0);

    uInt count = 0;

    if (csysSupports2DBox) {
        for (uInt i = 0; i < max(uInt(1), xCorners.size() / 2); i++) {
            for (uInt j = 0; j < max((uInt)1, polEndPts.size() / 2); j++) {
                for (uInt k = 0; k < max(uInt(1), chanEndPts.size() / 2); k++) {
                    if (csys.hasDirectionCoordinate() || csys.hasLinearCoordinate()) {
                        extXCorners[2 * count] = xCorners[2 * i];
                        extXCorners[2 * count + 1] = xCorners[2 * i + 1];
                        extYCorners[2 * count] = yCorners[2 * i];
                        extYCorners[2 * count + 1] = yCorners[2 * i + 1];
                    }
                    if (csys.hasPolarizationCoordinate()) {
                        extPolEndPts[2 * count] = polEndPtsDouble[2 * j];
                        extPolEndPts[2 * count + 1] = polEndPtsDouble[2 * j + 1];
                    }
                    if (csys.hasSpectralAxis()) {
                        extChanEndPts[2 * count] = chanEndPts[2 * k];
                        extChanEndPts[2 * count + 1] = chanEndPts[2 * k + 1];
                    }
                    count++;
                }
            }
        }
    } else {
        // here we have neither a direction nor linear coordinate with two
        // pixel axes which are greater than 0
        for (uInt j = 0; j < max((uInt)1, polEndPts.size() / 2); j++) {
            for (uInt k = 0; k < max(uInt(1), chanEndPts.size() / 2); k++) {
                if (csys.hasPolarizationCoordinate()) {
                    extPolEndPts[2 * count] = polEndPtsDouble[2 * j];
                    extPolEndPts[2 * count + 1] = polEndPtsDouble[2 * j + 1];
                }
                if (csys.hasSpectralAxis()) {
                    extChanEndPts[2 * count] = chanEndPts[2 * k];
                    extChanEndPts[2 * count + 1] = chanEndPts[2 * k + 1];
                }
                count++;
            }
        }
    }

    map<uInt, Vector<Double> > axisCornerMap;
    for (uInt i = 0; i < nRegions; i++) {
        for (uInt axisNumber = 0; axisNumber < csys.nPixelAxes(); axisNumber++) {
            if ((directionAxisNumbers.size() > 1 && (Int)axisNumber == directionAxisNumbers[0]) ||
                (!csys.hasDirectionCoordinate() && linearAxisNumbers.size() > 1 && (Int)axisNumber == linearAxisNumbers[0])) {
                axisCornerMap[axisNumber] = extXCorners;
            } else if ((directionAxisNumbers.size() > 1 && (Int)axisNumber == directionAxisNumbers[1]) ||
                       (!csys.hasDirectionCoordinate() && linearAxisNumbers.size() > 1 && (Int)axisNumber == linearAxisNumbers[1])) {
                axisCornerMap[axisNumber] = extYCorners;
            } else if ((Int)axisNumber == spectralAxisNumber) {
                axisCornerMap[axisNumber] = extChanEndPts;
            } else if ((Int)axisNumber == polarizationAxisNumber) {
                axisCornerMap[axisNumber] = extPolEndPts;
            } else {
                Vector<Double> range(2, 0);
                range[1] = imShape[axisNumber] - 1;
                axisCornerMap[axisNumber] = range;
            }
        }
    }

    // Set results
    ImageRegion imRegion;
    for (uInt i = 0; i < nRegions; i++) {
        for (uInt axisNumber = 0; axisNumber < csys.nPixelAxes(); axisNumber++) {
            blc(axisNumber) = axisCornerMap[axisNumber][2 * i];
            trc(axisNumber) = axisCornerMap[axisNumber][2 * i + 1];
        }

        LCBox lcBox(blc, trc, imShape);
        WCBox wcBox(lcBox, csys);
        ImageRegion thisRegion(wcBox);
        imRegion = (i == 0) ? thisRegion : imRegion = *(doUnion(imRegion, thisRegion));
    }

    ostringstream os;
    os << "Used image region from " << endl;
    if (csys.hasDirectionCoordinate()) {
        os << "    position box corners: ";
        for (uInt i = 0; i < boxCorners.size() / 4; i++) {
            os << boxCorners[4 * i] << ", " << boxCorners[4 * i + 1] << ", " << boxCorners[4 * i + 2] << ", " << boxCorners[4 * i + 3];
            if (i < boxCorners.size() / 4 - 1) {
                os << "; ";
            }
        }
    }

    if (getcoordsys().hasSpectralAxis()) {
        os << "    spectral channel ranges: " << _pairsToString(chanEndPts);
    }

    if (getcoordsys().hasPolarizationCoordinate()) {
        os << "    polarization pixel ranges: " << _pairsToString(polEndPts);
    }

    diagnostics = os.str();

    return imRegion;
}

String CasacRegionManager::_pairsToString(const vector<uInt>& pairs) const {
    ostringstream os;
    uInt nPairs = pairs.size() / 2;
    for (uInt i = 0; i < nPairs; i++) {
        os << pairs[2 * i] << " - " << pairs[2 * i + 1];
        if (i < nPairs - 1) {
            os << "; ";
        }
    }
    return os.str();
}

String CasacRegionManager::_stokesFromRecord(const Record& region, const StokesControl stokesControl, const IPosition& shape) const {
    // FIXME This implementation is incorrect for complex, recursive records
    String stokes = "";
    CoordinateSystem csys = getcoordsys();

    if (!csys.hasPolarizationCoordinate()) {
        return stokes;
    }

    Int polAxis = csys.polarizationAxisNumber();
    if (shape[polAxis] == 1) {
        // degenerate stokes axis
        return csys.stokesAtPixel(0);
    }

    uInt stokesBegin = 0;
    uInt stokesEnd = 0;
    if (region.empty()) {
        stokesEnd = stokesControl == USE_FIRST_STOKES ? 0 : csys.stokesCoordinate().stokes().size() - 1;
    } else {
        std::unique_ptr<ImageRegion> imreg(ImageRegion::fromRecord(region, ""));

        Array<Float> blc, trc;
        Bool oneRelAccountedFor = false;

        if (imreg->isLCSlicer()) {
            blc = imreg->asLCSlicer().blc();
            if ((Int)blc.size() <= polAxis) {
                *_getLog() << LogIO::WARN << "Cannot determine stokes. "
                           << "blc of input region does not include the polarization coordinate." << LogIO::POST;
                return stokes;
            }

            trc = imreg->asLCSlicer().trc();
            if ((Int)trc.size() <= polAxis) {
                *_getLog() << LogIO::WARN << "Cannot determine stokes. "
                           << "trc of input region does not include the polarization coordinate." << LogIO::POST;
                return stokes;
            }

            stokesBegin = (uInt)((Vector<Float>)blc)[polAxis];
            stokesEnd = (uInt)((Vector<Float>)trc)[polAxis];
            oneRelAccountedFor = true;

        } else if (RegionManager::isPixelRegion(*(ImageRegion::fromRecord(region, "")))) {
            region.toArray("blc", blc);
            region.toArray("trc", trc);
            stokesBegin = (uInt)((Vector<Float>)blc)[polAxis];
            stokesEnd = (uInt)((Vector<Float>)trc)[polAxis];

        } else if (region.fieldNumber("x") >= 0 && region.fieldNumber("y") >= 0) {
            // world polygon
            oneRelAccountedFor = true;
            stokesBegin = 0;
            stokesEnd = stokesControl == USE_FIRST_STOKES ? 0 : shape[polAxis];

        } else if (region.fieldNumber("blc") >= 0 && region.fieldNumber("blc") >= 0) {
            // world box
            Record blcRec = region.asRecord("blc");
            Record trcRec = region.asRecord("trc");
            String polField = "*" + String::toString(polAxis + 1);
            stokesBegin = blcRec.isDefined(polField) ? (Int)blcRec.asRecord(String(polField)).asDouble("value") : 0;
            stokesEnd = trcRec.isDefined(polField) ? (Int)blcRec.asRecord(String(polField)).asDouble("value")
                                                   : stokesControl == USE_FIRST_STOKES ? 0 : shape[polAxis];
            if (!blcRec.isDefined(polField)) {
                oneRelAccountedFor = true;
            }

        } else {
            // FIXME not very nice, but until all can be implemented this will have to do
            *_getLog() << LogIO::WARN << "Stokes cannot be determined because this "
                       << "region type is not handled yet. But chances are very good this is no need to be alarmed." << LogIO::POST;
            return stokes;
        }

        if (!oneRelAccountedFor && region.isDefined("oneRel") && region.asBool("oneRel")) {
            stokesBegin--;
            stokesEnd--;
        }
    }

    for (uInt i = stokesBegin; i <= stokesEnd; i++) {
        stokes += csys.stokesAtPixel(i);
    }

    return stokes;
}

vector<uInt> CasacRegionManager::_initSpectralRanges(uInt& nSelectedChannels, const IPosition& imShape) const {
    vector<uInt> ranges(0);
    if (!getcoordsys().hasSpectralAxis()) {
        nSelectedChannels = 0;
        return ranges;
    }
    uInt specNum = getcoordsys().spectralAxisNumber();
    ThrowIf(specNum >= imShape.size(), "Spectral axis number " + String::toString(specNum) +
                                           " must be less than number of shape dimensions " + String::toString(imShape.size()));
    uInt nChannels = imShape[getcoordsys().spectralAxisNumber()];
    ranges.push_back(0);
    ranges.push_back(nChannels - 1);
    nSelectedChannels = nChannels;
    return ranges;
}

vector<uInt> CasacRegionManager::setSpectralRanges(uInt& nSelectedChannels, const Record* const regionRec, const IPosition& imShape) const {
    if (regionRec == 0 || !getcoordsys().hasSpectralAxis()) {
        return _initSpectralRanges(nSelectedChannels, imShape);
    } else {
        return _spectralRangeFromRegionRecord(nSelectedChannels, regionRec, imShape);
    }
}

vector<uInt> CasacRegionManager::setSpectralRanges(String specification, uInt& nSelectedChannels,
    /*const String& globalChannelOverride,
    const String& globalStokesOverride, */
    const IPosition& imShape) const {
    LogOrigin origin("CasacRegionManager", __func__);
    *_getLog() << origin;
    specification.trim();
    String x = specification;
    x.upcase();
    if (x.empty() || x == ALL) {
        return _initSpectralRanges(nSelectedChannels, imShape);
    } else if (!getcoordsys().hasSpectralAxis()) {
        *_getLog() << LogIO::WARN << "Channel specification is "
                   << "not empty but the coordinate system has no spectral axis."
                   << "Channel specification will be ignored" << LogIO::POST;
        nSelectedChannels = 0;
        return vector<uInt>(0);
    } else if (specification.contains("range")) {
        // this is a specification in the "new" ASCII region format
        return _spectralRangeFromRangeFormat(nSelectedChannels, specification, imShape);
    } else {
        uInt nChannels = imShape[getcoordsys().spectralAxisNumber()];
        return casa::ParameterParser::spectralRangesFromChans(nSelectedChannels, specification, nChannels);
    }
}

vector<uInt> CasacRegionManager::_spectralRangeFromRegionRecord(
    uInt& nSelectedChannels, const Record* const regionRec, const IPosition& imShape) const {
    const CoordinateSystem& csys = getcoordsys();
    TempImage<Float> x(imShape, csys);
    x.set(0);
    casa::SPCIIF subimage = casa::SubImageFactory<Float>::createSubImageRO(x, *regionRec, "", _getLog(), AxesSpecifier(), false, true);
    uInt nChan = 0;
    {
        casa::ImageMetaData<Float> md(subimage);
        nChan = md.nChannels();
    }
    const SpectralCoordinate& subsp = subimage->coordinates().spectralCoordinate();
    Double subworld;
    subsp.toWorld(subworld, 0);
    const SpectralCoordinate& imsp = csys.spectralCoordinate();
    Double pixOff;
    imsp.toPixel(pixOff, subworld);
    Int specAxisNumber = csys.spectralAxisNumber();
    IPosition start(subimage->ndim(), 0);
    ArrayLattice<Bool> mask(subimage->getMask());
    IPosition planeShape = subimage->shape();
    vector<uInt> myList;
    planeShape[specAxisNumber] = 1;
    for (uInt i = 0; i < nChan; i++) {
        start[specAxisNumber] = i;
        Array<Bool> maskSlice;
        mask.getSlice(maskSlice, start, planeShape);
        if (anyTrue(maskSlice)) {
            uInt real = i + (uInt)rint(pixOff);
            myList.push_back(real);
            myList.push_back(real);
        }
    }
    return casa::ParameterParser::consolidateAndOrderRanges(nSelectedChannels, myList);
}

vector<uInt> CasacRegionManager::_spectralRangeFromRangeFormat(
    uInt& nSelectedChannels, const String& specification, const IPosition& imShape /*, const String& globalChannelOverride,
                                                          const String& globalStokesOverride */
) const {
    Bool spectralParmsUpdated;
    // check and make sure there are no disallowed parameters
    const CoordinateSystem csys = getcoordsys();
    casa::RegionTextParser::ParamSet parms = casa::RegionTextParser::getParamSet(spectralParmsUpdated, *_getLog(), specification, "", csys,
        std::shared_ptr<std::pair<MFrequency, MFrequency> >(nullptr), std::shared_ptr<Vector<Stokes::StokesTypes> >(nullptr));
    casa::RegionTextParser::ParamSet::const_iterator end = parms.end();
    for (casa::RegionTextParser::ParamSet::const_iterator iter = parms.begin(); iter != end; iter++) {
        casa::AnnotationBase::Keyword key = iter->first;
        ThrowIf(key != casa::AnnotationBase::FRAME && key != casa::AnnotationBase::RANGE && key != casa::AnnotationBase::VELTYPE &&
                    key != casa::AnnotationBase::RESTFREQ,
            "Non-frequency related keyword '" + casa::AnnotationBase::keywordToString(key) + "' found.");
    }
    // Parameters OK. We need to modify the input string so we can construct an AnnRegion
    // from which to get the spectral range information
    String regSpec = "box[[0pix, 0pix], [1pix, 1pix]] " + specification;
    casa::RegionTextParser parser(csys, imShape, regSpec);
    vector<uInt> range(2);
    ThrowIf(parser.getLines().empty(), "The specified spectral range " + specification + " does not intersect the image spectral range.");
    auto ann = parser.getLines()[0].getAnnotationBase();
    /*const AnnRegion*/ auto* reg = dynamic_cast<const casa::AnnRegion*>(ann.get());
    ThrowIf(!reg, "Dynamic cast failed");
    /*vector<Double>*/ const auto drange = reg->getSpectralPixelRange();
    range[0] = uInt(max(0.0, floor(drange[0] + 0.5)));
    range[1] = uInt(floor(drange[1] + 0.5));
    nSelectedChannels = range[1] - range[0] + 1;
    return range;
}

} // namespace carta
