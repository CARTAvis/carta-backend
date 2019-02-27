//# FileInfoLoader.cc: fill FileInfoExtended for all supported file types

#include "FileInfoLoader.h"
#include "ImageData/HDF5Attributes.h"

#include <algorithm>
#include <regex>
#include <fmt/format.h>

#include <casacore/casa/OS/File.h>
#include <casacore/casa/OS/Directory.h>
#include <casacore/fits/FITS/hdu.h>
#include <casacore/fits/FITS/fitsio.h>
#include <casacore/fits/FITS/FITSTable.h>
#include <casacore/casa/HDF5/HDF5File.h>
#include <casacore/casa/HDF5/HDF5Group.h>
#include <casacore/casa/HDF5/HDF5Error.h>
#include <casacore/images/Images/ImageSummary.h>
#include <casacore/images/Images/FITSImage.h>
#include <casacore/images/Images/MIRIADImage.h>
#include <casacore/images/Images/PagedImage.h>
#include <casacore/mirlib/miriad.h>
#include <casacore/lattices/Lattices/HDF5Lattice.h>

//#################################################################################
// FILE INFO LOADER

FileInfoLoader::FileInfoLoader(const std::string& filename) :
    m_file(filename) {
    m_type = fileType(filename);
}

casacore::ImageOpener::ImageTypes
FileInfoLoader::fileType(const std::string &file) {
    return casacore::ImageOpener::imageType(file);
}

//#################################################################################
// FILE INFO

bool FileInfoLoader::fillFileInfo(CARTA::FileInfo* fileInfo) {
    casacore::File ccfile(m_file);
    if (!ccfile.exists()) {
        return false;
    }

    // fill FileInfo submessage
    int64_t fileInfoSize(ccfile.size());
    if (ccfile.isDirectory()) { // symlinked dirs are dirs
        casacore::Directory ccdir(ccfile);
        fileInfoSize = ccdir.size();
    } else if (ccfile.isSymLink()) {  // gets size of link not file
        casacore::String resolvedFileName(ccfile.path().resolvedName());
        casacore::File linkedfile(resolvedFileName);
        fileInfoSize = linkedfile.size();
    }

    fileInfo->set_size(fileInfoSize);
    fileInfo->set_name(ccfile.path().baseName());
    fileInfo->set_type(convertFileType(m_type));
    casacore::String absFileName(ccfile.path().absoluteName());
    return getHduList(fileInfo, absFileName);
}

CARTA::FileType FileInfoLoader::convertFileType(int ccImageType) {
    // convert casacore ImageType to protobuf FileType
    switch (ccImageType) {
        case casacore::ImageOpener::FITS:
            return CARTA::FileType::FITS;
        case casacore::ImageOpener::AIPSPP:
            return CARTA::FileType::CASA;
        case casacore::ImageOpener::HDF5:
            return CARTA::FileType::HDF5;
        case casacore::ImageOpener::MIRIAD:
            return CARTA::FileType::MIRIAD;
        default:
            return CARTA::FileType::UNKNOWN;
    }
}

bool FileInfoLoader::getHduList(CARTA::FileInfo* fileInfo, const std::string& filename) {
    // fill FileInfo hdu list
    bool hduOK(true);
    if (fileInfo->type()==CARTA::FileType::HDF5) {
        casacore::HDF5File hdfFile(filename);
        std::vector<casacore::String> hdus(casacore::HDF5Group::linkNames(hdfFile));
        for (auto groupName : hdus) {
            fileInfo->add_hdu_list(groupName);
        }
        hduOK = (fileInfo->hdu_list_size() > 0);
    } else if (fileInfo->type()==CARTA::FITS) {
        casacore::FitsInput fInput(filename.c_str(), casacore::FITS::Disk, 10, fileInfoFitsErrHandler);
        if (fInput.err() == casacore::FitsIO::OK) { // check for cfitsio error
            int numHdu(fInput.getnumhdu());
            hduOK = (numHdu > 0);
            if (hduOK) {
                for (int hdu=0; hdu<numHdu; ++hdu)
                    fileInfo->add_hdu_list(casacore::String::toString(hdu));
            }
        } else {
            hduOK = false;
        }
    } else {
        fileInfo->add_hdu_list("");
    }
    return hduOK;
}


//#################################################################################
// FILE INFO EXTENDED

bool FileInfoLoader::fillFileExtInfo(CARTA::FileInfoExtended* extInfo, std::string& hdu, std::string& message) {
    casacore::File ccfile(m_file);
    if (!ccfile.exists()) {
        return false;
    }

    bool extInfoOK(false);
    std::string name(ccfile.path().baseName());
    auto entry = extInfo->add_computed_entries();
    entry->set_name("Name");
    entry->set_value(name);
    entry->set_entry_type(CARTA::EntryType::STRING);

    // fill FileExtInfo depending on image type
    switch(m_type) {
    case casacore::ImageOpener::AIPSPP:
        extInfoOK = fillCASAExtFileInfo(extInfo, message);
        break;
    case casacore::ImageOpener::FITS:
        extInfoOK = fillFITSExtFileInfo(extInfo, hdu, message);
        break;
    case casacore::ImageOpener::HDF5:
        extInfoOK = fillHdf5ExtFileInfo(extInfo, hdu, message);
        break;
    case casacore::ImageOpener::MIRIAD:
        extInfoOK = fillCASAExtFileInfo(extInfo, message);
        break;
    default:
        break;
    }
    return extInfoOK;
}

// HDF5
bool FileInfoLoader::fillHdf5ExtFileInfo(CARTA::FileInfoExtended* extendedInfo, std::string& hdu, std::string& message) {
    // Add extended info for HDF5 file
    try {
        // read attributes into casacore Record
        casacore::HDF5File hdfFile(m_file);
        casacore::HDF5Group hdfGroup(hdfFile, hdu, true);
        casacore::Record attributes;
        try {
            attributes = HDF5Attributes::doReadAttributes(hdfGroup.getHid());
        } catch (casacore::HDF5Error& err) {
            message = "Error reading HDF5 attributes: " + err.getMesg();
            hdfGroup.close();
            hdfFile.close();
            return false;
        }
        hdfGroup.close();
        hdfFile.close();
        if (attributes.empty()) {
            message = "No HDF5 attributes";
            return false;
        }

        // check dimensions
        casacore::uInt ndim;
        casacore::IPosition dataShape;
        try {
            casacore::HDF5Lattice<float> hdf5Latt = casacore::HDF5Lattice<float>(m_file, "DATA", hdu);
            dataShape = hdf5Latt.shape();
            ndim = dataShape.size();
        } catch (casacore::AipsError& err) {
            message = "Cannot open HDF5 DATA dataset: " + err.getMesg();
            // TODO: how to close the file here?
            return false;
        }
        if (ndim < 2 || ndim > 4) {
            message = "Image must be 2D, 3D or 4D.";
            return false;
        }

        // extract values from Record
        for (casacore::uInt field=0; field<attributes.nfields(); ++field) {
            auto headerEntry = extendedInfo->add_header_entries();
            headerEntry->set_name(attributes.name(field));
            switch (attributes.type(field)) {
                case casacore::TpString: {
                    *headerEntry->mutable_value() = attributes.asString(field);
                    headerEntry->set_entry_type(CARTA::EntryType::STRING);
                    }
                    break;
                case casacore::TpInt64: {
                    casacore::Int64 valueInt = attributes.asInt64(field);
                    *headerEntry->mutable_value() = fmt::format("{}", valueInt);
                    headerEntry->set_entry_type(CARTA::EntryType::INT);
                    headerEntry->set_numeric_value(valueInt);
                    }
                    break;
                case casacore::TpDouble: {
                    casacore::Double numericVal = attributes.asDouble(field);
                    *headerEntry->mutable_value() = fmt::format("{}", numericVal);
                    headerEntry->set_entry_type(CARTA::EntryType::FLOAT);
                    headerEntry->set_numeric_value(numericVal);
                    }
                    break;
                default:
                    break;
            }
        }

        // If in header, get values for computed entries:
        // Get string values
        std::string coordTypeX, coordTypeY, coordType3, coordType4, radeSys, equinox, specSys,
            bunit, crpix1, crpix2, cunit1, cunit2;
        if (attributes.isDefined("CTYPE1"))
            coordTypeX = attributes.asString("CTYPE1");
        if (attributes.isDefined("CTYPE2"))
            coordTypeY = attributes.asString("CTYPE2");
        if (attributes.isDefined("CTYPE3"))
            coordType3 = attributes.asString("CTYPE3");
        if (attributes.isDefined("CTYPE4"))
            coordType4 = attributes.asString("CTYPE4");
        if (attributes.isDefined("RADESYS"))
            radeSys = attributes.asString("RADESYS");
        if (attributes.isDefined("SPECSYS"))
            specSys = attributes.asString("SPECSYS");
        if (attributes.isDefined("BUNIT"))
            bunit = attributes.asString("BUNIT");
        if (attributes.isDefined("CUNIT1"))
            cunit1 = attributes.asString("CUNIT1");
        if (attributes.isDefined("CUNIT2"))
            cunit2 = attributes.asString("CUNIT2");
        // Convert numeric values to string
        double val;
        bool ok;
        ok = getDoubleAttribute(val, attributes, "EQUINOX");
        if (ok) equinox = casacore::String::toString(val);
        ok = getDoubleAttribute(val, attributes, "CRPIX1");
        if (ok) crpix1 = casacore::String::toString(val);
        ok = getDoubleAttribute(val, attributes, "CRPIX2");
        if (ok) crpix2 = casacore::String::toString(val);
        // Get numeric values
        double crval1 = (getDoubleAttribute(val, attributes, "CRVAL1") ? val : 0.0);
        double crval2 = (getDoubleAttribute(val, attributes, "CRVAL2") ? val : 0.0);
        double cdelt1 = (getDoubleAttribute(val, attributes, "CDELT1") ? val : 0.0);
        double cdelt2 = (getDoubleAttribute(val, attributes, "CDELT2") ? val : 0.0);
        double bmaj = (getDoubleAttribute(val, attributes, "BMAJ") ? val : 0.0);
        double bmin = (getDoubleAttribute(val, attributes, "BMIN") ? val : 0.0);
        double bpa = (getDoubleAttribute(val, attributes, "BPA") ? val : 0.0);

        // shape, chan, stokes entries first
        int chanAxis, stokesAxis;
        findChanStokesAxis(dataShape, coordTypeX, coordTypeY, coordType3, coordType4, chanAxis, stokesAxis);
        addShapeEntries(extendedInfo, dataShape, chanAxis, stokesAxis);

        // make computed entries strings
        std::string xyCoords, crPixels, crCoords, crDegStr, cr1, cr2, axisInc, rsBeam;
        if (!coordTypeX.empty() && !coordTypeY.empty())
            xyCoords = fmt::format("{}, {}", coordTypeX, coordTypeY);
        if (!crpix1.empty() && !crpix2.empty())
            crPixels = fmt::format("[{}, {}] ", crpix1, crpix2);
        if (!(crval1 == 0.0 && crval2 == 0.0)) 
            crCoords = fmt::format("[{:.4f} {}, {:.4f} {}]", crval1, cunit1, crval2, cunit2);
        cr1 = makeValueStr(coordTypeX, crval1, cunit1);
        cr2 = makeValueStr(coordTypeY, crval2, cunit2);
        crDegStr = fmt::format("[{}, {}]", cr1, cr2);
        if (!(cdelt1 == 0.0 && cdelt2 == 0.0))
            axisInc = fmt::format("{}, {}", unitConversion(cdelt1, cunit1), unitConversion(cdelt2, cunit2));
        if (!(bmaj == 0.0 && bmin == 0.0 && bpa == 0.0))
            rsBeam = deg2arcsec(bmaj) + " X " + deg2arcsec(bmin) +  fmt::format(", {:.4f} deg", bpa);
        makeRadesysStr(radeSys, equinox);

        // fill computed_entries
        addComputedEntries(extendedInfo, xyCoords, crPixels, crCoords, crDegStr,
            radeSys, specSys, bunit, axisInc, rsBeam);
    } catch (casacore::AipsError& err) {
        message = "Error opening HDF5 file";
        return false;
    }
    return true;
}

// FITS
bool FileInfoLoader::fillFITSExtFileInfo(CARTA::FileInfoExtended* extendedInfo, string& hdu, string& message) {
    bool extInfoOK(true);
    try {
        // convert string hdu to unsigned int
        casacore::String ccHdu(hdu);
        casacore::uInt hdunum;
        ccHdu.fromString(hdunum, true);

        // check shape
        casacore::Int ndim(0);
        casacore::IPosition dataShape;
        try {
            casacore::FITSImage fitsImg(m_file, 0, hdunum);
            dataShape = fitsImg.shape();
            ndim = dataShape.size();
            if (ndim < 2 || ndim > 4) {
                message = "Image must be 2D, 3D or 4D.";
                return false;
            }
        } catch (casacore::AipsError& err) {
            message = err.getMesg();
            if (message.find("diagonal") != std::string::npos) // "ArrayBase::diagonal() - diagonal out of range"
                message = "Failed to open image at specified HDU.";
            else if (message.find("No image at specified location") != std::string::npos)
                message = "No image at specified HDU.";
            return false;
        }
        extendedInfo->set_dimensions(ndim);

        // use FITSTable to get Record of hdu entries
        hdunum += 1;  // FITSTable starts at 1
        casacore::FITSTable fitsTable(m_file, hdunum, true); 
        casacore::Record hduEntries(fitsTable.primaryKeywords().toRecord());
        // set dims
        extendedInfo->set_width(dataShape(0));
        extendedInfo->set_height(dataShape(1));
        extendedInfo->add_stokes_vals(""); // not in header

        // if in header, save values for computed entries
        std::string coordTypeX, coordTypeY, coordType3, coordType4, radeSys, equinox, specSys,
            bunit, crpix1, crpix2, cunit1, cunit2;
        double crval1(0.0), crval2(0.0), cdelt1(0.0), cdelt2(0.0), bmaj(0.0), bmin(0.0), bpa(0.0);

        // set header entries 
        for (casacore::uInt field=0; field < hduEntries.nfields(); ++field) {
            casacore::String name = hduEntries.name(field);
            if ((name!="SIMPLE") && (name!="BITPIX") && !name.startsWith("PC")) {
                auto headerEntry = extendedInfo->add_header_entries();
                headerEntry->set_name(name);
                casacore::DataType dtype(hduEntries.type(field));
                switch (dtype) {
                    case casacore::TpString: {
                        *headerEntry->mutable_value() = hduEntries.asString(field);
                        headerEntry->set_entry_type(CARTA::EntryType::STRING);
                        if (headerEntry->name() == "CTYPE1") 
                            coordTypeX = headerEntry->value();
                        else if (headerEntry->name() == "CTYPE2")
                            coordTypeY = headerEntry->value();
                        else if (headerEntry->name() == "CTYPE3")
                            coordType3 = headerEntry->value();
                        else if (headerEntry->name() == "CTYPE4")
                            coordType4 = headerEntry->value();
                        else if (headerEntry->name() == "RADESYS") 
                            radeSys = headerEntry->value();
                        else if (headerEntry->name() == "SPECSYS") 
                            specSys = headerEntry->value();
                        else if (headerEntry->name() == "BUNIT") 
                            bunit = headerEntry->value();
                        else if (headerEntry->name() == "CUNIT1") 
                            cunit1 = headerEntry->value();
                        else if (headerEntry->name() == "CUNIT2") 
                            cunit2 = headerEntry->value();
                        break;
                    }
                    case casacore::TpInt: {
                        int64_t valueInt(hduEntries.asInt(field));
                        *headerEntry->mutable_value() = fmt::format("{}", valueInt);
                        headerEntry->set_entry_type(CARTA::EntryType::INT);
                        headerEntry->set_numeric_value(valueInt);
                        break;
                    }
                    case casacore::TpFloat:
                    case casacore::TpDouble: {
                        double numericValue(hduEntries.asDouble(field));
                        *headerEntry->mutable_value() = fmt::format("{}", numericValue);
                        headerEntry->set_entry_type(CARTA::EntryType::FLOAT);
                        headerEntry->set_numeric_value(numericValue);
                        if (headerEntry->name() == "EQUINOX")
                            equinox = std::to_string(static_cast<int>(numericValue));
                        else if (headerEntry->name() == "CRVAL1")
                            crval1 = numericValue;
                        else if (headerEntry->name() == "CRVAL2")
                            crval2 = numericValue;
                        else if (headerEntry->name() == "CRPIX1")
                            crpix1 = std::to_string(static_cast<int>(numericValue));
                        else if (headerEntry->name() == "CRPIX2")
                            crpix2 = std::to_string(static_cast<int>(numericValue));
                        else if (headerEntry->name() == "CDELT1")
                            cdelt1 = numericValue;
                        else if (headerEntry->name() == "CDELT2")
                            cdelt2 = numericValue;
                        else if (headerEntry->name() == "BMAJ")
                            bmaj = numericValue;
                        else if (headerEntry->name() == "BMIN")
                            bmin = numericValue;
                        else if (headerEntry->name() == "BPA")
                            bpa = numericValue;
                        break;
                    }
                    default:
                        break;
                }
            }
        }

        // shape, chan, stokes entries first
        int chanAxis, stokesAxis;
        findChanStokesAxis(dataShape, coordTypeX, coordTypeY, coordType3, coordType4, chanAxis, stokesAxis);
        addShapeEntries(extendedInfo, dataShape, chanAxis, stokesAxis);

        // make strings for computed entries
        std::string xyCoords, crPixels, crCoords, cr1, cr2, crDegStr, axisInc, rsBeam;
        if (!coordTypeX.empty() && !coordTypeY.empty())
            xyCoords = fmt::format("{}, {}", coordTypeX, coordTypeY);
        if (!crpix1.empty() && !crpix2.empty())
            crPixels = fmt::format("[{}, {}] ", crpix1, crpix2);
        if (crval1!=0.0 && crval2!=0.0) 
            crCoords = fmt::format("[{:.4f} {}, {:.4f} {}]", crval1, cunit1, crval2, cunit2);
        cr1 = makeValueStr(coordTypeX, crval1, cunit1);
        cr2 = makeValueStr(coordTypeY, crval2, cunit2);
        crDegStr = fmt::format("[{}, {}]", cr1, cr2);
        if (!(cdelt1 == 0.0 && cdelt2 == 0.0)) 
            axisInc = fmt::format("{}, {}", unitConversion(cdelt1, cunit1), unitConversion(cdelt2, cunit2));
        if (!(bmaj == 0.0 && bmin == 0.0 && bpa == 0.0))
            rsBeam = deg2arcsec(bmaj) + " X " + deg2arcsec(bmin) +  fmt::format(", {:.4f} deg", bpa);
        makeRadesysStr(radeSys, equinox);

        // fill computed_entries
        addComputedEntries(extendedInfo, xyCoords, crPixels, crCoords, crDegStr,
            radeSys, specSys, bunit, axisInc, rsBeam);
    } catch (casacore::AipsError& err) {
        message = err.getMesg();
        extInfoOK = false;
    }
    return extInfoOK;
}

// CASA, MIRIAD
bool FileInfoLoader::fillCASAExtFileInfo(CARTA::FileInfoExtended* extendedInfo, std::string& message) {
    bool extInfoOK(true);
    casacore::ImageInterface<casacore::Float>* ccImage(nullptr);
    try {
        switch (m_type) {
            case casacore::ImageOpener::AIPSPP: {
                ccImage = new casacore::PagedImage<casacore::Float>(m_file);
                break;
            }
            case casacore::ImageOpener::MIRIAD: {
                // no way to catch error, use casacore miriad lib directly
                int thandle, ihandle, iostat, ndim;
                hopen_c(&thandle, m_file.c_str(), "old", &iostat);
                if (iostat != 0) {
                    message = "Could not open MIRIAD file";
                    return false;
                }
                haccess_c(thandle, &ihandle, "image", "read", &iostat);
                if (iostat != 0) {
                    message = "Could not open MIRIAD file";
                    return false;
                }
                rdhdi_c(thandle, "naxis", &ndim, 0);  // read "naxis" value into ndim, default 0
                hdaccess_c(ihandle, &iostat);
                hclose_c(thandle);
                if (ndim < 2 || ndim > 4) {
                    message = "Image must be 2D, 3D or 4D.";
                    return false;
                }
                // hopefully okay to open now as casacore Image
                ccImage = new casacore::MIRIADImage(m_file);
                break;
            }
            default:
                break;
        }
        if (ccImage == nullptr) {
            message = "Unable to open image.";
            return false;
        }
        // objects to retrieve header information
        casacore::ImageInfo imInfo(ccImage->imageInfo());
        casacore::ImageSummary<casacore::Float> imSummary(*ccImage);
        casacore::CoordinateSystem coordsys(ccImage->coordinates());

        // ndim, shape
        casacore::Int ndim(imSummary.ndim());
        extendedInfo->set_dimensions(ndim);
        if (ndim < 2 || ndim > 4) {
            message = "Image must be 2D, 3D or 4D.";
            return false;
        }
        casacore::IPosition dataShape(imSummary.shape());
        extendedInfo->set_width(dataShape(0));
        extendedInfo->set_height(dataShape(1));
        extendedInfo->add_stokes_vals(""); // not in header
        // set dims in header entries
        auto headerEntry = extendedInfo->add_header_entries();
        headerEntry->set_name("NAXIS");
        *headerEntry->mutable_value() = fmt::format("{}", ndim);
        headerEntry->set_entry_type(CARTA::EntryType::INT);
        headerEntry->set_numeric_value(ndim);
        for (casacore::Int i=0; i<ndim; ++i) {
            auto headerEntry = extendedInfo->add_header_entries();
            headerEntry->set_name("NAXIS"+ casacore::String::toString(i+1));
            *headerEntry->mutable_value() = fmt::format("{}", dataShape(i));
            headerEntry->set_entry_type(CARTA::EntryType::INT);
            headerEntry->set_numeric_value(dataShape(i));
        }

        // if in header, save values for computed entries
        std::string rsBeam, bunit, projection, equinox, radeSys, 
            coordTypeX, coordTypeY, coordType3, coordType4, specSys;

        // BMAJ, BMIN, BPA
        if (imInfo.hasBeam() && imInfo.hasSingleBeam()) {
            // get values
            casacore::GaussianBeam rbeam(imInfo.restoringBeam());
            casacore::Quantity majAx(rbeam.getMajor()), minAx(rbeam.getMinor()),
                pa(rbeam.getPA(true));
            majAx.convert("deg");
            minAx.convert("deg");
            pa.convert("deg");

            // add to header entries
            casacore::Double bmaj(majAx.getValue()), bmin(minAx.getValue());
            casacore::Float bpa(pa.getValue());
            headerEntry = extendedInfo->add_header_entries();
            headerEntry->set_name("BMAJ");
            *headerEntry->mutable_value() = fmt::format("{}", bmaj);
            headerEntry->set_entry_type(CARTA::EntryType::FLOAT);
            headerEntry->set_numeric_value(bmaj);
            headerEntry = extendedInfo->add_header_entries();
            headerEntry->set_name("BMIN");
            *headerEntry->mutable_value() = fmt::format("{}", bmin);
            headerEntry->set_entry_type(CARTA::EntryType::FLOAT);
            headerEntry->set_numeric_value(bmin);
            headerEntry = extendedInfo->add_header_entries();
            headerEntry->set_name("BPA");
            *headerEntry->mutable_value() = fmt::format("{}", bpa);
            headerEntry->set_entry_type(CARTA::EntryType::FLOAT);
            headerEntry->set_numeric_value(bpa);

            // add to computed entries
            rsBeam = fmt::format("{} X {}, {:.4f} deg", deg2arcsec(bmaj), deg2arcsec(bmin), bpa);
        }

        // type
        headerEntry = extendedInfo->add_header_entries();
        headerEntry->set_name("BTYPE");
        *headerEntry->mutable_value() = casacore::ImageInfo::imageType(imInfo.imageType());
        headerEntry->set_entry_type(CARTA::EntryType::STRING);
        // object
        headerEntry = extendedInfo->add_header_entries();
        headerEntry->set_name("OBJECT");
        *headerEntry->mutable_value() = imInfo.objectName();
        headerEntry->set_entry_type(CARTA::EntryType::STRING);
        // units
        headerEntry = extendedInfo->add_header_entries();
        headerEntry->set_name("BUNIT");
        bunit = imSummary.units().getName();
        *headerEntry->mutable_value() = bunit;
        headerEntry->set_entry_type(CARTA::EntryType::STRING);

        // Direction axes: projection, equinox, radesys
        casacore::Vector<casacore::String> dirAxisNames;  // axis names to append projection
        casacore::MDirection::Types dirType;
        casacore::Int iDirCoord(coordsys.findCoordinate(casacore::Coordinate::DIRECTION));
        if (iDirCoord >= 0) {
            const casacore::DirectionCoordinate& dirCoord = coordsys.directionCoordinate(casacore::uInt(iDirCoord));
            // direction axes
            projection = dirCoord.projection().name();
            dirAxisNames = dirCoord.worldAxisNames();
            // equinox, radesys
            dirCoord.getReferenceConversion(dirType);
            equinox = casacore::MDirection::showType(dirType);
            getRadesysFromEquinox(radeSys, equinox);

        }

        // get imSummary axes values: name, reference pixel and value, pixel increment, units
        casacore::Vector<casacore::String> axNames(imSummary.axisNames());
        casacore::Vector<casacore::Double> axRefPix(imSummary.referencePixels());
        casacore::Vector<casacore::Double> axRefVal(imSummary.referenceValues());
        casacore::Vector<casacore::Double> axInc(imSummary.axisIncrements());
        casacore::Vector<casacore::String> axUnits(imSummary.axisUnits());
        size_t axisSize(axNames.size());
        for (casacore::uInt i=0; i<axisSize; ++i) {
            casacore::String suffix(casacore::String::toString(i+1)); // append to keyword name
            casacore::String axisName = axNames(i);
            // modify direction axes, if any
            if ((iDirCoord >= 0) && anyEQ(dirAxisNames, axisName)) {
                convertAxisName(axisName, projection, dirType);  // FITS name with projection
                if (axUnits(i)=="rad") { // convert ref value and increment to deg
                    casacore::Quantity refvalQuant(axRefVal(i), axUnits(i));
                    refvalQuant.convert("deg");
                    casacore::Quantity incQuant(axInc(i), axUnits(i));
                    incQuant.convert("deg");
                    // update values to use later for computed entries
                    axRefVal(i) = refvalQuant.getValue();
                    axInc(i) = incQuant.getValue();
                    axUnits(i) = incQuant.getUnit();
                }
            }
            // name = CTYPE
            // add header entry
            headerEntry = extendedInfo->add_header_entries();
            headerEntry->set_name("CTYPE"+ suffix);
            headerEntry->set_entry_type(CARTA::EntryType::STRING);
            *headerEntry->mutable_value() = axisName;
            // computed entries
            if (suffix=="1")
                coordTypeX = axisName;
            else if (suffix=="2")
                coordTypeY = axisName;
            else if (suffix=="3")
                coordType3 = axisName;
            else if (suffix=="4")
                coordType4 = axisName;

            // ref val = CRVAL
            headerEntry = extendedInfo->add_header_entries();
            headerEntry->set_name("CRVAL"+ suffix);
            *headerEntry->mutable_value() = fmt::format("{}", axRefVal(i));
            headerEntry->set_entry_type(CARTA::EntryType::FLOAT);
            headerEntry->set_numeric_value(axRefVal(i));
            // increment = CDELT
            headerEntry = extendedInfo->add_header_entries();
            headerEntry->set_name("CDELT"+ suffix);
            *headerEntry->mutable_value() = fmt::format("{}", axInc(i));
            headerEntry->set_entry_type(CARTA::EntryType::FLOAT);
            headerEntry->set_numeric_value(axInc(i));
            // ref pix = CRPIX
            headerEntry = extendedInfo->add_header_entries();
            headerEntry->set_name("CRPIX"+ suffix);
            *headerEntry->mutable_value() = fmt::format("{}", axRefPix(i));
            headerEntry->set_entry_type(CARTA::EntryType::FLOAT);
            headerEntry->set_numeric_value(axRefPix(i));
            // units = CUNIT
            headerEntry = extendedInfo->add_header_entries();
            headerEntry->set_name("CUNIT"+ suffix);
            *headerEntry->mutable_value() = axUnits(i);
            headerEntry->set_entry_type(CARTA::EntryType::STRING);
        }

        // RESTFRQ
        casacore::String restfreqStr;
        casacore::Quantum<casacore::Double> restFreq;
        casacore::Bool ok = imSummary.restFrequency(restfreqStr, restFreq);
        if (ok) {
            headerEntry = extendedInfo->add_header_entries();
            headerEntry->set_name("RESTFRQ");
            casacore::Double restFreqVal(restFreq.getValue());
            *headerEntry->mutable_value() = restfreqStr;
            headerEntry->set_entry_type(CARTA::EntryType::FLOAT);
            headerEntry->set_numeric_value(restFreqVal);
        }

        // SPECSYS
        casacore::Int iSpecCoord(coordsys.findCoordinate(casacore::Coordinate::SPECTRAL));
        if (iSpecCoord >= 0) {
            const casacore::SpectralCoordinate& spCoord = coordsys.spectralCoordinate(casacore::uInt(iSpecCoord));
            casacore::MFrequency::Types freqType;
            casacore::MEpoch epoch;
            casacore::MPosition position;
            casacore::MDirection direction;
            spCoord.getReferenceConversion(freqType, epoch, position, direction);
            bool haveSpecSys = convertSpecsysToFITS(specSys, freqType);
            if (haveSpecSys) {
                headerEntry = extendedInfo->add_header_entries();
                headerEntry->set_name("SPECSYS");
                *headerEntry->mutable_value() = specSys;
                headerEntry->set_entry_type(CARTA::EntryType::STRING);
            }
        }

        // RADESYS, EQUINOX
        if (!radeSys.empty()) {
            headerEntry = extendedInfo->add_header_entries();
            headerEntry->set_name("RADESYS");
            *headerEntry->mutable_value() = radeSys;
            headerEntry->set_entry_type(CARTA::EntryType::STRING);
        }
        if (!equinox.empty()) {
            headerEntry = extendedInfo->add_header_entries();
            headerEntry->set_name("EQUINOX");
            *headerEntry->mutable_value() = equinox;
            headerEntry->set_entry_type(CARTA::EntryType::STRING);
        }
        // computed entries
        makeRadesysStr(radeSys, equinox);

        // Other summary items
        // telescope
        headerEntry = extendedInfo->add_header_entries();
        headerEntry->set_name("TELESCOP");
        *headerEntry->mutable_value() = imSummary.telescope();
        headerEntry->set_entry_type(CARTA::EntryType::STRING);
        // observer
        headerEntry = extendedInfo->add_header_entries();
        headerEntry->set_name("OBSERVER");
        *headerEntry->mutable_value() = imSummary.observer();
        headerEntry->set_entry_type(CARTA::EntryType::STRING);
        // obs date
        casacore::MEpoch epoch;
        headerEntry = extendedInfo->add_header_entries();
        headerEntry->set_name("DATE");
        *headerEntry->mutable_value() = imSummary.obsDate(epoch);
        headerEntry->set_entry_type(CARTA::EntryType::STRING);

        // shape, chan, stokes entries first
        int chanAxis, stokesAxis;
        findChanStokesAxis(dataShape, coordTypeX, coordTypeY, coordType3, coordType4, chanAxis, stokesAxis);
        addShapeEntries(extendedInfo, dataShape, chanAxis, stokesAxis);

        // computed_entries
        std::string xyCoords, crPixels, crCoords, crDegStr, axisInc;
        int crpix0, crpix1;
        std::string cunit0, cr0, cunit1, cr1;
        double crval0, crval1, cdelt0, cdelt1;
        if (axisSize >= 1) {   // pv images only have first axis values
            crpix0 = static_cast<int>(axRefPix(0));
            cunit0 = axUnits(0);
            crval0 = axRefVal(0);
            cdelt0 = axInc(0);
            cr0 = makeValueStr(coordTypeX, crval0, cunit0);  // for angle format
            if (axisSize > 1) {
                crpix1 = static_cast<int>(axRefPix(1));
                cunit1 = axUnits(1);
                crval1 = axRefVal(1);
                cdelt1 = axInc(1);
                cr1 = makeValueStr(coordTypeY, crval1, cunit1);  // for angle format
                xyCoords = fmt::format("{}, {}", coordTypeX, coordTypeY);
                crPixels = fmt::format("[{}, {}]", crpix0, crpix1);
                crCoords = fmt::format("[{:.3f} {}, {:.3f} {}]", crval0, cunit0, crval1, cunit1);
                crDegStr = fmt::format("[{} {}]", cr0, cr1);
                axisInc = fmt::format("{}, {}", unitConversion(cdelt0, cunit0), unitConversion(cdelt1, cunit1));
            } else {
                xyCoords = fmt::format("{}", coordTypeX);
                crPixels = fmt::format("[{}]", crpix0);
                crCoords = fmt::format("[{:.3f} {}]", crval0, cunit0);
                crDegStr = fmt::format("[{}]", cr0);
                axisInc = fmt::format("{}", unitConversion(cdelt0, cunit0));
            }
        }
        addComputedEntries(extendedInfo, xyCoords, crPixels, crCoords, crDegStr, radeSys,
            specSys, bunit, axisInc, rsBeam);
    } catch (casacore::AipsError& err) {
        if (ccImage != nullptr) {
            delete ccImage;
        }
        message = err.getMesg().c_str();
        extInfoOK = false;
    }
    if (ccImage != nullptr) {
        delete ccImage;
    }
    return extInfoOK;
}


// ***** Computed entries *****

void FileInfoLoader::addComputedEntries(CARTA::FileInfoExtended* extendedInfo,
    const std::string& xyCoords, const std::string& crPixels, const std::string& crCoords,
    const std::string& crDeg, const std::string& radeSys, const std::string& specSys,
    const std::string& bunit, const std::string& axisInc, const std::string& rsBeam) {
    // add computed_entries to extended info (ensures the proper order in file browser)
    if (!xyCoords.empty()) {
        auto entry = extendedInfo->add_computed_entries();
        entry->set_name("Coordinate type");
        entry->set_value(xyCoords);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }

    if (!crPixels.empty()) {
        auto entry = extendedInfo->add_computed_entries();
        entry->set_name("Image reference pixels");
        entry->set_value(crPixels);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }

    if (!crCoords.empty()) {
        auto entry = extendedInfo->add_computed_entries();
        entry->set_name("Image reference coordinates");
        entry->set_value(crCoords);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }

    if (!crDeg.empty()) {
        auto entry = extendedInfo->add_computed_entries();
        entry->set_name("Image ref coords (coord type)");
        entry->set_value(crDeg);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }

    if (!radeSys.empty()) {
        auto entry = extendedInfo->add_computed_entries();
        entry->set_name("Celestial frame");
        entry->set_value(radeSys);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }

    if (!specSys.empty()) {
        auto entry = extendedInfo->add_computed_entries();
        entry->set_name("Spectral frame");
        entry->set_value(specSys);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }

    if (!bunit.empty()) {
        auto entry = extendedInfo->add_computed_entries();
        entry->set_name("Pixel unit");
        entry->set_value(bunit);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }

    if (!axisInc.empty()) {
        auto entry = extendedInfo->add_computed_entries();
        entry->set_name("Pixel increment");
        entry->set_value(axisInc);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }

    if (!rsBeam.empty()) {
        auto entry = extendedInfo->add_computed_entries();
        entry->set_name("Restoring beam");
        entry->set_value(rsBeam);
        entry->set_entry_type(CARTA::EntryType::STRING);
    }
}

// shape, nchan, nstokes
void FileInfoLoader::addShapeEntries(CARTA::FileInfoExtended* extendedInfo, const casacore::IPosition& shape,
     int chanAxis, int stokesAxis) {
    // Set fields/header entries for shape
 
    // dim, width, height, depth, stokes fields
    int ndim(shape.size());
    extendedInfo->set_dimensions(ndim);
    extendedInfo->set_width(shape(0));
    extendedInfo->set_height(shape(1));
    if (shape.size() == 2) { 
        extendedInfo->set_depth(1);
        extendedInfo->set_stokes(1);
    } else if (shape.size() == 3) {
        extendedInfo->set_depth(shape(2));
        extendedInfo->set_stokes(1);
    } else { // shape is 4
        if (chanAxis < 2) { // not found or is xy
            if (stokesAxis < 2) {  // not found or is xy, use defaults
                extendedInfo->set_depth(shape(2));
                extendedInfo->set_stokes(shape(3));
            } else { // stokes found, set depth to other one
                extendedInfo->set_stokes(shape(stokesAxis));
                if (stokesAxis==2) extendedInfo->set_depth(shape(3));
                else extendedInfo->set_depth(shape(2));
            }
        } else if (chanAxis >= 2) {  // chan found, set stokes to other one
            extendedInfo->set_depth(shape(chanAxis));
            // stokes axis is the other one
            if (chanAxis == 2) extendedInfo->set_stokes(shape(3));
            else extendedInfo->set_stokes(shape(2));
        }
    }

    // shape entry
    std::string shapeString;
    switch (ndim) {
        case 2:
            shapeString = fmt::format("[{}, {}]", shape(0), shape(1));
            break;
        case 3:
            shapeString = fmt::format("[{}, {}, {}]", shape(0), shape(1), shape(2));
            break;
        case 4:
            shapeString = fmt::format("[{}, {}, {}, {}]", shape(0), shape(1), shape(2), shape(3));
            break;
    }
    auto entry = extendedInfo->add_computed_entries();
    entry->set_name("Shape");
    entry->set_value(shapeString);
    entry->set_entry_type(CARTA::EntryType::STRING);

    // nchan, nstokes computed entries
    // set number of channels if chan axis exists or has 3rd axis
    if ((chanAxis >= 0) || (ndim >= 3)) {
        int nchan;
        if (chanAxis >= 0) nchan = shape(chanAxis);
        else nchan = extendedInfo->depth();
        // header entry for number of chan
        auto entry = extendedInfo->add_computed_entries();
        entry->set_name("Number of channels");
        entry->set_value(casacore::String::toString(nchan));
        entry->set_entry_type(CARTA::EntryType::INT);
        entry->set_numeric_value(nchan);
    }
    // set number of stokes if stokes axis exists or has 4th axis
    if ((stokesAxis >= 0) || (ndim > 3)) {
        int nstokes;
        if (stokesAxis >= 0) nstokes = shape(stokesAxis);
        else nstokes = extendedInfo->stokes();
        // header entry for number of stokes
        auto entry = extendedInfo->add_computed_entries();
        entry->set_name("Number of stokes");
        entry->set_value(casacore::String::toString(nstokes));
        entry->set_entry_type(CARTA::EntryType::INT);
        entry->set_numeric_value(nstokes);
    }
}

// For shape entries, determine spectral and stokes axes
void FileInfoLoader::findChanStokesAxis(const casacore::IPosition& dataShape, const casacore::String& axisType1,
    const casacore::String& axisType2, const casacore::String& axisType3, const casacore::String& axisType4,
    int& chanAxis, int& stokesAxis) {
    // Use CTYPE values to find axes and set nchan, nstokes
    // Note header axes are 1-based but shape is 0-based
    casacore::String cType1(axisType1), cType2(axisType2), cType3(axisType3), cType4(axisType4);
    // uppercase for string comparisons
    cType1.upcase();
    cType2.upcase();
    cType3.upcase();
    cType4.upcase();

    // find spectral axis
    if (!cType1.empty() &&
        (cType1.contains("FELO") || cType1.contains("FREQ") || cType1.contains("VELO") ||
         cType1.contains("VOPT") || cType1.contains("VRAD") || cType1.contains("WAVE") ||
         cType1.contains("AWAV")) ) {
        chanAxis = 0;
    } else if (!cType2.empty() &&
        (cType2.contains("FELO") || cType2.contains("FREQ") || cType2.contains("VELO") ||
         cType2.contains("VOPT") || cType2.contains("VRAD") || cType2.contains("WAVE") ||
         cType2.contains("AWAV")) ) {
        chanAxis = 1;
    } else if (!cType3.empty() &&
        (cType3.contains("FELO") || cType3.contains("FREQ") || cType3.contains("VELO") ||
         cType3.contains("VOPT") || cType3.contains("VRAD") || cType3.contains("WAVE") ||
         cType3.contains("AWAV")) ) {
        chanAxis = 2;
    } else if (!cType4.empty() &&
        (cType4.contains("FELO") || cType4.contains("FREQ") || cType4.contains("VELO") ||
         cType4.contains("VOPT") || cType4.contains("VRAD") || cType4.contains("WAVE") ||
         cType4.contains("AWAV"))) {
        chanAxis = 3;
    } else {
        chanAxis = -1;
    }

    // find stokes axis
    if (cType1 == "STOKES")
        stokesAxis = 0;
    else if (cType2 == "STOKES")
        stokesAxis = 1;
    else if (cType3 == "STOKES")
        stokesAxis = 2;
    else if (cType4 == "STOKES")
        stokesAxis = 3;
    else 
        stokesAxis = -1;
}


// ***** casacore::Record attributes *****

// get int value
bool FileInfoLoader::getIntAttribute(casacore::Int64& val, const casacore::Record& rec,
        const casacore::String& field) {
    bool getOK(true);
    if (rec.isDefined(field)) {
        try {
            val = rec.asInt64(field);
        } catch (casacore::AipsError& err) {
            try {
                val = casacore::String::toInt(rec.asString(field));
            } catch (casacore::AipsError& err) {
                getOK = false;
            }
        }
    } else {
        getOK = false;
    }
    return getOK;
}

// get double value
bool FileInfoLoader::getDoubleAttribute(casacore::Double& val, const casacore::Record& rec,
        const casacore::String& field) {
    bool getOK(true);
    if (rec.isDefined(field)) {
        try {
            val = rec.asDouble(field);
        } catch (casacore::AipsError& err) {
            try {
                val = casacore::String::toDouble(rec.asString(field));
            } catch (casacore::AipsError& err) {
                getOK = false;
            }
        }
    } else {
        getOK = false;
    }
    return getOK;
}


// ***** FITS keyword conversion *****

void FileInfoLoader::convertAxisName(std::string& axisName, std::string& projection, casacore::MDirection::Types type) {
    // Convert direction axis name to FITS name and append projection
    if ((axisName == "Right Ascension") || (axisName == "Hour Angle")) {
        axisName = (projection.empty() ? "RA" : "RA---" + projection);
    } else if (axisName == "Declination") {
        axisName = (projection.empty() ? "DEC" : "DEC--" + projection);
    } else if (axisName == "Longitude") {
        switch(type) {
            case casacore::MDirection::GALACTIC: 
                axisName = (projection.empty() ? "GLON" : "GLON-" + projection);
                break;
            case casacore::MDirection::SUPERGAL: 
                axisName = (projection.empty() ? "SLON" : "SLON-" + projection);
                break;
            case casacore::MDirection::ECLIPTIC: 
            case casacore::MDirection::MECLIPTIC: 
            case casacore::MDirection::TECLIPTIC: 
                axisName = (projection.empty() ? "ELON" : "ELON-" + projection);
                break;
            default:
                break;
        }
    } else if (axisName == "Latitude") {
        switch(type) {
            case casacore::MDirection::GALACTIC: 
                axisName = (projection.empty() ? "GLAT" : "GLAT-" + projection);
                break;
            case casacore::MDirection::SUPERGAL: 
                axisName = (projection.empty() ? "SLAT" : "SLAT-" + projection);
                break;
            case casacore::MDirection::ECLIPTIC: 
            case casacore::MDirection::MECLIPTIC: 
            case casacore::MDirection::TECLIPTIC: 
                axisName = (projection.empty() ? "ELAT" : "ELAT-" + projection);
                break;
            default:
                break;
        }
    }
}

void FileInfoLoader::makeRadesysStr(std::string& radeSys, const std::string& equinox) {
    // append equinox to radesys
    std::string prefix;
    if (!equinox.empty()) {
        if ((radeSys.compare("FK4")==0) && (equinox[0]!='B'))
            prefix = "B";
        else if ((radeSys.compare("FK5")==0) && (equinox[0]!='J'))
            prefix = "J";
    }
    if (!radeSys.empty() && !equinox.empty())
        radeSys.append(", ");
    radeSys.append(prefix + equinox);
}

void FileInfoLoader::getRadesysFromEquinox(std::string& radeSys, std::string& equinox) {
    // according to casacore::FITSCoordinateUtil::toFITSHeader
    if (equinox.find("ICRS") != std::string::npos) {
        radeSys = "ICRS";
        equinox = "2000";
    } else if (equinox.find("2000") != std::string::npos) {
        radeSys = "FK5";
    } else if (equinox.find("B1950") != std::string::npos) {
        radeSys = "FK4";
    }
}
        
std::string FileInfoLoader::makeValueStr(const std::string& type, double val, const std::string& unit) {
    // make coordinate angle string for RA, DEC, GLON, GLAT; else just return "{val} {unit}"
    std::string valStr;
    if (unit.empty()) {
        valStr = fmt::format("{} {}", val, unit);
    } else {
        // convert to uppercase for string comparisons
        std::string upperType(type);
        transform(upperType.begin(), upperType.end(), upperType.begin(), ::toupper);
        bool isRA(upperType.find("RA") != std::string::npos);
        bool isAngle((upperType.find("DEC") != std::string::npos) ||
            (upperType.find("LON") != std::string::npos) || (upperType.find("LAT") != std::string::npos));
        if (isRA || isAngle) {
            casacore::MVAngle::formatTypes format(casacore::MVAngle::ANGLE);
            if (isRA) format = casacore::MVAngle::TIME;
            casacore::Quantity quant1(val, unit);
            casacore::MVAngle mva(quant1);
            valStr = mva.string(format, 10);
        } else {
            valStr = fmt::format("{} {}", val, unit);
        }
    }
    return valStr;
}

bool FileInfoLoader::convertSpecsysToFITS(std::string& specsys, casacore::MFrequency::Types type) {
    // use labels in FITS headers
    bool result(true);
    switch (type) {
        case casacore::MFrequency::LSRK:
            specsys = "LSRK";
            break;
        case casacore::MFrequency::BARY:
            specsys = "BARYCENT";
            break;
        case casacore::MFrequency::LSRD:
            specsys = "LSRD";
            break;
        case casacore::MFrequency::GEO:
            specsys = "GEOCENTR";
            break;
        case casacore::MFrequency::REST:
            specsys = "SOURCE";
            break;
        case casacore::MFrequency::GALACTO:
            specsys = "GALACTOC";
            break;
        case casacore::MFrequency::LGROUP:
            specsys = "LOCALGRP";
            break;
        case casacore::MFrequency::CMB:
            specsys = "CMBDIPOL";
            break;
        case casacore::MFrequency::TOPO:
            specsys = "TOPOCENT";
            break;
        default:
            specsys = "";
            result = false;
    }
    return result;
}


// ***** Unit conversion *****

std::string FileInfoLoader::unitConversion(const double value, const std::string& unit) {
    if (std::regex_match(unit, std::regex("rad", std::regex::icase))) {
        return deg2arcsec(rad2deg(value));
    } else if (std::regex_match(unit, std::regex("deg", std::regex::icase))) {
        return deg2arcsec(value);
    } else if (std::regex_match(unit, std::regex("hz", std::regex::icase))) {
        return convertHz(value);
    } else if (std::regex_match(unit, std::regex("arcsec", std::regex::icase))) {
        return fmt::format("{:.3f}\"", value);
    } else { // unknown
        return fmt::format("{:.3f} {}", value, unit);
    }
}

// Unit conversion: convert radians to degree
double FileInfoLoader::rad2deg(const double rad) {
    return 57.29577951 * rad;
}

// Unit conversion: convert degree to arcsec
std::string FileInfoLoader::deg2arcsec(const double degree) {
    // 1 degree = 60 arcmin = 60*60 arcsec
    double arcs = fabs(degree * 3600);

    // customized format of arcsec
    if (arcs >= 60.0){ // arcs >= 60, convert to arcmin
        return fmt::format("{:.2f}\'", degree < 0 ? -1*arcs/60 : arcs/60);
    } else if (arcs < 60.0 && arcs > 0.1) { // 0.1 < arcs < 60
        return fmt::format("{:.2f}\"", degree < 0 ? -1*arcs : arcs);
    } else if (arcs <= 0.1 && arcs > 0.01) { // 0.01 < arcs <= 0.1
        return fmt::format("{:.3f}\"", degree < 0 ? -1*arcs : arcs);
    } else { // arcs <= 0.01
        return fmt::format("{:.4f}\"", degree < 0 ? -1*arcs : arcs);
    }
}

// Unit conversion: convert Hz to MHz or GHz
std::string FileInfoLoader::convertHz(const double hz) {
    if (hz >= 1.0e9) {
        return fmt::format("{:.4f} GHz", hz/1.0e9);
    } else if (hz < 1.0e9 && hz >= 1.0e6) {
        return fmt::format("{:.4f} MHz", hz/1.0e6);
    } else {
        return fmt::format("{:.4f} Hz", hz);
    }
}
