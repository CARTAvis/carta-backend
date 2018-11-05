//# FileInfoLoader.cc: fill FileInfoExtended for all supported file types

#include "FileInfoLoader.h"

#include <algorithm>
#include <fmt/format.h>

#include <casacore/casa/OS/File.h>
#include <casacore/fits/FITS/FITSTable.h>
#include <casacore/casa/HDF5/HDF5File.h>
#include <casacore/casa/HDF5/HDF5Group.h>
#include <casacore/casa/HDF5/HDF5Error.h>
#include <casacore/casa/HDF5/HDF5DataSet.h>
#include <casacore/images/Images/ImageSummary.h>
#include <casacore/images/Images/FITSImgParser.h>
#include <casacore/images/Images/MIRIADImage.h>
#include <casacore/images/Images/PagedImage.h>

using namespace std;
using namespace CARTA;


//#################################################################################
// HDF5Attributes class

casacore::Record HDF5Attributes::doReadAttributes (hid_t groupHid) {
    // reads attributes but not links
    casacore::Record rec;
    char cname[512];
    int nfields = H5Aget_num_attrs (groupHid);
    // Iterate through the attributes in order of index, so we're sure
    // they are read back in the same order as written.
    for (int index=0; index<nfields; ++index) {
        casacore::HDF5HidAttribute id(H5Aopen_idx(groupHid, index));
        AlwaysAssert (id.getHid()>=0, casacore::AipsError);
        unsigned int namsz = H5Aget_name(id, sizeof(cname), cname);
        AlwaysAssert (namsz<sizeof(cname), casacore::AipsError);
        casacore::String name(cname);
        // Get rank and shape from the dataspace info.
        casacore::HDF5HidDataSpace dsid (H5Aget_space(id));
        int rank = H5Sget_simple_extent_ndims(dsid);
        if (rank > 0) {
          casacore::Block<hsize_t> shp(rank);
          rank = H5Sget_simple_extent_dims(dsid, shp.storage(), NULL);
        }
        // Get data type and its size.
        if (rank == 0) {
          casacore::HDF5HidDataType dtid(H5Aget_type(id));
          readScalar (id, dtid, name, rec);
        }
    }
    return rec;
}

void HDF5Attributes::readScalar (hid_t attrId, hid_t dtid, const casacore::String& name,
    casacore::RecordInterface& rec) {
    // Handle a scalar field.
    int sz = H5Tget_size(dtid);
    switch (H5Tget_class(dtid)) {
        case H5T_INTEGER: {
            casacore::Int64 value;
            casacore::HDF5DataType dtype((casacore::Int64*)0);
            H5Aread(attrId, dtype.getHidMem(), &value);
            rec.define(name, value);
            }
            break;
        case H5T_FLOAT: {
            casacore::Double value;
            casacore::HDF5DataType dtype((casacore::Double*)0);
            H5Aread(attrId, dtype.getHidMem(), &value);
            rec.define(name, value);
            }
            break;
        case H5T_STRING: {
            casacore::String value;
            value.resize(sz+1);
            casacore::HDF5DataType dtype(value);
            H5Aread(attrId, dtype.getHidMem(), const_cast<char*>(value.c_str()));
            value.resize(sz);
            rec.define(name, value);
            }
            break;
        default: 
           throw casacore::HDF5Error ("Unknown data type of scalar attribute " + name);
    }
}

//#################################################################################
// FILE INFO LOADER

FileInfoLoader::FileInfoLoader(const string& filename) :
    m_file(filename) {
    m_type = fileType(filename);
}

casacore::ImageOpener::ImageTypes
FileInfoLoader::fileType(const std::string &file) {
    return casacore::ImageOpener::imageType(file);
}

//#################################################################################
// FILE INFO

bool FileInfoLoader::fillFileInfo(FileInfo* fileInfo) {
    // fill FileInfo submessage
    casacore::File ccfile(m_file);
    fileInfo->set_size(ccfile.size());
    fileInfo->set_name(ccfile.path().baseName());
    casacore::String absFileName(ccfile.path().absoluteName());
    fileInfo->set_type(convertFileType(m_type));
    return getHduList(fileInfo, absFileName);
}

CARTA::FileType FileInfoLoader::convertFileType(int ccImageType) {
    // convert casacore ImageType to protobuf FileType
    switch (ccImageType) {
        case casacore::ImageOpener::FITS:
            return FileType::FITS;
        case casacore::ImageOpener::AIPSPP:
            return FileType::CASA;
        case casacore::ImageOpener::HDF5:
            return FileType::HDF5;
        case casacore::ImageOpener::MIRIAD:
            return FileType::MIRIAD;
        default:
            return FileType::UNKNOWN;
    }
}

bool FileInfoLoader::getHduList(FileInfo* fileInfo, const std::string& filename) {
    // fill FileInfo hdu list
    bool hduOK(true);
    if (fileInfo->type()==CARTA::HDF5) {
        casacore::HDF5File hdfFile(filename);
        std::vector<casacore::String> hdus(casacore::HDF5Group::linkNames(hdfFile));
        for (auto groupName : hdus) {
            fileInfo->add_hdu_list(groupName);
        }
        hduOK = (fileInfo->hdu_list_size() > 0);
    } else if (fileInfo->type()==CARTA::FITS) {
        casacore::FITSImgParser fitsParser(filename.c_str());
        int numHdu(fitsParser.get_numhdu());
        for (int hdu=0; hdu<numHdu; ++hdu) {
            fileInfo->add_hdu_list(casacore::String::toString(hdu));
        }
        hduOK = (fileInfo->hdu_list_size() > 0);
    } else {
        fileInfo->add_hdu_list("");
    }
    return hduOK;
}

//#################################################################################
// FILE INFO EXTENDED

// get int value
bool FileInfoLoader::getIntAttribute(casacore::Int64& val, const casacore::Record& rec, const casacore::String& field) {
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
bool FileInfoLoader::getDoubleAttribute(casacore::Double& val, const casacore::Record& rec, const casacore::String& field) {
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

// make strings for computed entries
void FileInfoLoader::makeRadesysStr(std::string& radeSys, const std::string& equinox) {
    // append equinox to radesys
    if (!radeSys.empty() && !equinox.empty()) {
        string prefix;
        if (radeSys == ("FK4")) prefix="B";
        else if (radeSys == ("FK5")) prefix="J";
        radeSys.append(", " + prefix + equinox);
    }
}

std::string FileInfoLoader::makeDegStr(const std::string& xType, double crval1, double crval2,
    const std::string& cunit1, const std::string& cunit2) {
    // make coordinate angle string for (RA, DEC), (GLON, GLAT)
    std::string degStr;
    if (!cunit1.empty() && !cunit2.empty()) {
        if ((xType.find("RA") == std::string::npos) && (xType.find("GLON") == std::string::npos))
            return degStr; // no deg string for e.g. VOPT
        casacore::MVAngle::formatTypes xformat(casacore::MVAngle::ANGLE);
        if (xType.find("RA") != std::string::npos)
            xformat = casacore::MVAngle::TIME;
        casacore::Quantity quant1(crval1, cunit1);
        casacore::MVAngle mva1(quant1);
        std::string crtime1(mva1.string(xformat, 10));
        casacore::Quantity quant2(crval2, cunit2);
        casacore::MVAngle mva2(quant2);
        std::string crtime2(mva2.string(casacore::MVAngle::ANGLE, 10));
        degStr = fmt::format("[{}, {}]", crtime1, crtime2);
    }
    return degStr;
}

// fill FileExtInfo depending on image type
bool FileInfoLoader::fillFileExtInfo(FileInfoExtended* extInfo, string& hdu, string& message) {
    bool extInfoOK(false);
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

bool FileInfoLoader::fillHdf5ExtFileInfo(FileInfoExtended* extendedInfo, string& hdu, string& message) {
    // Add extended info for HDF5 file
    try {
        casacore::HDF5File hdfFile(m_file);
        casacore::HDF5Group hdfGroup(hdfFile, hdu, true);
        casacore::Record attributes;
        try {
            attributes = HDF5Attributes::doReadAttributes(hdfGroup.getHid());
        } catch (casacore::HDF5Error& err) {
            message = "Error reading attributes: " + err.getMesg();
            hdfGroup.close();
            hdfFile.close();
            return false;
        }
        hdfGroup.close();
        hdfFile.close();
        if (attributes.empty())
            return false;

        // check dimensions
        casacore::Int64 ndims(0);
        casacore::IPosition dataShape;
        if (attributes.isDefined("NAXIS")) {
            bool ok = getIntAttribute(ndims, attributes, "NAXIS");
        } else {
            try {
                casacore::HDF5DataSet dataSet(hdfGroup, "DATA", (casacore::Float*)0);
                dataShape = dataSet.shape();
                ndims = dataShape.size();
            } catch (casacore::AipsError& err) {
                message = "HDF5 file is missing DATA dataset";
                return false;
            }
        }
        if (ndims < 2 || ndims > 4) {
            message = "Image must be 2D, 3D or 4D.";
            return false;
        }
        extendedInfo->set_dimensions(ndims);

        // extract values from Record
        for (casacore::uInt field=0; field<attributes.nfields(); ++field) {
            auto headerEntry = extendedInfo->add_header_entries();
            headerEntry->set_name(attributes.name(field));
            switch (attributes.type(field)) {
                case casacore::TpString: {
                    *headerEntry->mutable_value() = attributes.asString(field);
                    headerEntry->set_entry_type(EntryType::STRING);
                    }
                    break;
                case casacore::TpInt64: {
                    casacore::Int64 valueInt = attributes.asInt64(field);
                    *headerEntry->mutable_value() = fmt::format("{}", valueInt);
                    headerEntry->set_entry_type(EntryType::INT);
                    headerEntry->set_numeric_value(valueInt);
                    }
                    break;
                case casacore::TpDouble: {
                    casacore::Double numericVal = attributes.asDouble(field);
                    *headerEntry->mutable_value() = fmt::format("{}", numericVal);
                    headerEntry->set_entry_type(EntryType::FLOAT);
                    headerEntry->set_numeric_value(numericVal);
                    }
                    break;
                default:
                    break;
            }
        }

        // width, height; depth, stokes for 3D image
        casacore::Int64 naxis3(-1), naxis4(-1);
        if (dataShape.size() > 0) {
            extendedInfo->set_width(dataShape(0));
            extendedInfo->set_height(dataShape(1));
            if (ndims == 3) {
                extendedInfo->set_depth(dataShape(2));
                extendedInfo->set_stokes(1);
            }
        } else {
            casacore::Int64 naxis;
            bool ok;
            ok = getIntAttribute(naxis, attributes, "NAXIS1");
            if (ok) extendedInfo->set_width(naxis);
            ok = getIntAttribute(naxis, attributes, "NAXIS2");
            if (ok) extendedInfo->set_height(naxis);
            if (ndims > 2) {
                ok = getIntAttribute(naxis3, attributes, "NAXIS3");
                if (ok && ndims==3) {
                    extendedInfo->set_depth(naxis3);
                    extendedInfo->set_stokes(1);
                }
            }
            if (ndims > 3)
                ok = getIntAttribute(naxis4, attributes, "NAXIS4");
        }

        // if in header, get values for computed entries
        // string values
        std::string coordinateTypeX, coordinateTypeY, coordinateType4, radeSys, equinox,
            specSys, bunit, crpix1, crpix2, cunit1, cunit2;
        if (attributes.isDefined("CTYPE1"))
            coordinateTypeX = attributes.asString("CTYPE1");
        if (attributes.isDefined("CTYPE2"))
            coordinateTypeY = attributes.asString("CTYPE2");
        if (attributes.isDefined("CTYPE4"))
            coordinateType4 = attributes.asString("CTYPE4");
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
        // convert numeric to string
        double val;
        bool ok;
        ok = getDoubleAttribute(val, attributes, "EQUINOX");
        if (ok) equinox = casacore::String::toString(val);
        ok = getDoubleAttribute(val, attributes, "CRPIX1");
        if (ok) crpix1 = casacore::String::toString(val);
        ok = getDoubleAttribute(val, attributes, "CRPIX2");
        if (ok) crpix2 = casacore::String::toString(val);
        // numeric values
        double crval1 = (getDoubleAttribute(val, attributes, "CRVAL1") ? val : 0.0);
        double crval2 = (getDoubleAttribute(val, attributes, "CRVAL2") ? val : 0.0);
        double cdelt1 = (getDoubleAttribute(val, attributes, "CDELT1") ? val : 0.0);
        double cdelt2 = (getDoubleAttribute(val, attributes, "CDELT2") ? val : 0.0);

        // depth, stokes for 4D image
        bool stokesIsAxis4(true);  // could be axis 3 or 4
        if (ndims == 4) {
            int axis3size(dataShape.size() > 0 ? dataShape(2) : naxis3);
            int axis4size(dataShape.size() > 0 ? dataShape(3) : naxis4);
            if (coordinateType4=="STOKES") { 
                extendedInfo->set_depth(axis3size);
                extendedInfo->set_stokes(axis4size);
            } else {
                extendedInfo->set_depth(axis4size);
                extendedInfo->set_stokes(axis3size);
                stokesIsAxis4 = false;
            }
        }

        // make computed entries strings
        std::string crPixels, crCoords, crDegStr, axisInc;
        if (!crpix1.empty() && !crpix2.empty())
            crPixels = fmt::format("[{}, {}] ", crpix1, crpix2);
        if (!(crval1 == 0.0 && crval2 == 0.0)) 
            crCoords = fmt::format("[{:.4f} {}, {:.4f} {}]", crval1, cunit1, crval2, cunit2);
        crDegStr = makeDegStr(coordinateTypeX, crval1, crval2, cunit1, cunit2);
        if (!(cdelt1 == 0.0 && cdelt2 == 0.0)) 
            axisInc = fmt::format("{} {}, {} {}", cdelt1, cunit1, cdelt2, cunit2);
        makeRadesysStr(radeSys, equinox);

        // fill computed_entries
        addComputedEntries(extendedInfo, coordinateTypeX, coordinateTypeY, crPixels, crCoords, crDegStr,
            radeSys, specSys, bunit, axisInc, stokesIsAxis4);
    } catch (casacore::AipsError& err) {
        message = "Error opening HDF5 file";
        return false;
    }
    return true;
}

bool FileInfoLoader::fillFITSExtFileInfo(FileInfoExtended* extendedInfo, string& hdu, string& message) {
    bool extInfoOK(true);
    try {
        // convert string hdu to unsigned int
        casacore::String ccHdu(hdu);
        casacore::uInt hdunum;
        ccHdu.fromString(hdunum, true);
        hdunum += 1;  // FITSTable starts at 1

        // use FITSTable to get Record of hdu entries
        casacore::FITSTable fitsTable(m_file, hdunum, true); 
        casacore::Record hduEntries(fitsTable.primaryKeywords().toRecord());
        // set dims
        casacore::Int dim = hduEntries.asInt("NAXIS");
        extendedInfo->set_dimensions(dim);
        if (dim < 2 || dim > 4) {
            message = "Image must be 2D, 3D or 4D.";
            return false;
        }
        extendedInfo->set_width(hduEntries.asInt("NAXIS1"));
        extendedInfo->set_height(hduEntries.asInt("NAXIS2"));
        extendedInfo->add_stokes_vals(""); // not in header

        // if in header, save values for computed entries
        std::string coordinateTypeX, coordinateTypeY, coordinateType4, radeSys, equinox, specSys,
            bunit, crpix1, crpix2, cunit1, cunit2;
        double crval1(0.0), crval2(0.0), cdelt1(0.0), cdelt2(0.0);

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
                        headerEntry->set_entry_type(EntryType::STRING);
                        if (headerEntry->name() == "CTYPE1") 
                            coordinateTypeX = headerEntry->value();
                        else if (headerEntry->name() == "CTYPE2")
                            coordinateTypeY = headerEntry->value();
                        else if (headerEntry->name() == "CTYPE4")
                            coordinateType4 = headerEntry->value();
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
                        headerEntry->set_entry_type(EntryType::INT);
                        headerEntry->set_numeric_value(valueInt);
                        break;
                    }
                    case casacore::TpFloat:
                    case casacore::TpDouble: {
                        double numericValue(hduEntries.asDouble(field));
                        *headerEntry->mutable_value() = fmt::format("{}", numericValue);
                        headerEntry->set_entry_type(EntryType::FLOAT);
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
                        break;
                    }
                    default:
                        break;
                }
            }
        }
        // depth, stokes
        bool stokesIsAxis4(true);
        if (dim<4) {
            extendedInfo->set_depth(dim > 2 ? hduEntries.asInt("NAXIS3") : 1);
            extendedInfo->set_stokes(1);
        } else {
            transform(coordinateType4.begin(), coordinateType4.end(), coordinateType4.begin(), ::toupper);
            if (coordinateType4=="STOKES") {
                extendedInfo->set_depth(hduEntries.asInt("NAXIS3"));
                extendedInfo->set_stokes(hduEntries.asInt("NAXIS4"));
            } else {
                extendedInfo->set_depth(hduEntries.asInt("NAXIS4"));
                extendedInfo->set_stokes(hduEntries.asInt("NAXIS3"));
                stokesIsAxis4 = false;
            }
        }
        // make strings for computed entries
        std::string crPixels, crCoords, crDegStr, axisInc;
        if (!crpix1.empty() && !crpix2.empty())
            crPixels = fmt::format("[{}, {}] ", crpix1, crpix2);
        if (crval1!=0.0 && crval2!=0.0) 
            crCoords = fmt::format("[{:.4f} {}, {:.4f} {}]", crval1, cunit1, crval2, cunit2);
        crDegStr = makeDegStr(coordinateTypeX, crval1, crval2, cunit1, cunit2);
        if (!(cdelt1 == 0.0 && cdelt2 == 0.0)) 
            axisInc = fmt::format("{} {}, {} {}", cdelt1, cunit1, cdelt2, cunit2);
        makeRadesysStr(radeSys, equinox);

        // fill computed_entries
        addComputedEntries(extendedInfo, coordinateTypeX, coordinateTypeY, crPixels, crCoords, crDegStr,
            radeSys, specSys, bunit, axisInc, stokesIsAxis4);
    } catch (casacore::AipsError& err) {
        message = err.getMesg();
        extInfoOK = false;
    }
    return extInfoOK;
}

bool FileInfoLoader::fillCASAExtFileInfo(FileInfoExtended* extendedInfo, string& message) {
    bool extInfoOK(true);
    casacore::ImageInterface<casacore::Float>* ccImage(nullptr);
    try {
        switch (m_type) {
            case casacore::ImageOpener::AIPSPP: {
                ccImage = new casacore::PagedImage<casacore::Float>(m_file);
                break;
            }
            case casacore::ImageOpener::MIRIAD: {
                ccImage = new casacore::MIRIADImage(m_file);
                break;
            }
            default:
                break;
        }
        casacore::ImageInfo imInfo(ccImage->imageInfo());
        casacore::ImageSummary<casacore::Float> imSummary(*ccImage);
        // set dim
        casacore::Int dim(imSummary.ndim());
        extendedInfo->set_dimensions(dim);
        if (dim < 2 || dim > 4) {
            message = "Image must be 2D, 3D or 4D.";
            return false;
        }
        casacore::IPosition imShape(imSummary.shape());
        extendedInfo->set_width(imShape(0));
        extendedInfo->set_height(imShape(1));
        extendedInfo->add_stokes_vals(""); // not in header

        // if in header, save values for computed entries
        std::string coordinateTypeX, coordinateTypeY, coordinateType4, radeSys, specSys, bunit;

        // set dims in header entries
        auto headerEntry = extendedInfo->add_header_entries();
        headerEntry->set_name("NAXIS");
        *headerEntry->mutable_value() = fmt::format("{}", dim);
        headerEntry->set_entry_type(EntryType::INT);
        headerEntry->set_numeric_value(dim);
        for (casacore::Int i=0; i<dim; ++i) {
            auto headerEntry = extendedInfo->add_header_entries();
            headerEntry->set_name("NAXIS"+ casacore::String::toString(i+1));
            *headerEntry->mutable_value() = fmt::format("{}", imShape(i));
            headerEntry->set_entry_type(EntryType::INT);
            headerEntry->set_numeric_value(imShape(i));
        }
        // BMAJ, BMIN, BPA
        if (imInfo.hasBeam() && imInfo.hasSingleBeam()) {
            // get values
            casacore::GaussianBeam rbeam(imInfo.restoringBeam());
            casacore::Quantity majAx(rbeam.getMajor()), minAx(rbeam.getMinor()),
                pa(rbeam.getPA(true));
            majAx.convert("deg");
            minAx.convert("deg");
            pa.convert("deg");
            if (majAx.getValue()<1.0 || minAx.getValue()<1.0) {
                majAx.convert(casacore::Unit("arcsec"));
                minAx.convert(casacore::Unit("arcsec"));
            }
            // add to header entries
            casacore::Double bmaj(majAx.getValue()), bmin(minAx.getValue());
            casacore::Float bpa(pa.getValue());
            headerEntry = extendedInfo->add_header_entries();
            headerEntry->set_name("BMAJ");
            *headerEntry->mutable_value() = fmt::format("{}", bmaj);
            headerEntry->set_entry_type(EntryType::FLOAT);
            headerEntry->set_numeric_value(bmaj);
            headerEntry = extendedInfo->add_header_entries();
            headerEntry->set_name("BMIN");
            *headerEntry->mutable_value() = fmt::format("{}", bmin);
            headerEntry->set_entry_type(EntryType::FLOAT);
            headerEntry->set_numeric_value(bmin);
            headerEntry = extendedInfo->add_header_entries();
            headerEntry->set_name("BPA");
            *headerEntry->mutable_value() = fmt::format("{}", bpa);
            headerEntry->set_entry_type(EntryType::FLOAT);
            headerEntry->set_numeric_value(bpa);
        }
        // type
        headerEntry = extendedInfo->add_header_entries();
        headerEntry->set_name("BTYPE");
        *headerEntry->mutable_value() = casacore::ImageInfo::imageType(imInfo.imageType());
        headerEntry->set_entry_type(EntryType::STRING);
        // object
        headerEntry = extendedInfo->add_header_entries();
        headerEntry->set_name("OBJECT");
        *headerEntry->mutable_value() = imInfo.objectName();
        headerEntry->set_entry_type(EntryType::STRING);
        // units
        headerEntry = extendedInfo->add_header_entries();
        headerEntry->set_name("BUNIT");
        bunit = imSummary.units().getName();
        *headerEntry->mutable_value() = bunit;
        headerEntry->set_entry_type(EntryType::STRING);
        // axes values
        casacore::Vector<casacore::String> axNames(imSummary.axisNames());
        casacore::Vector<casacore::Double> axRefPix(imSummary.referencePixels());
        casacore::Vector<casacore::Double> axRefVal(imSummary.referenceValues());
        casacore::Vector<casacore::Double> axInc(imSummary.axisIncrements());
        casacore::Vector<casacore::String> axUnits(imSummary.axisUnits());
        size_t axisSize(axNames.size());
        for (casacore::uInt i=0; i<axisSize; ++i) {
            casacore::String suffix(casacore::String::toString(i+1));
            // name = CTYPE
            headerEntry = extendedInfo->add_header_entries();
            headerEntry->set_name("CTYPE"+ suffix);
            headerEntry->set_entry_type(EntryType::STRING);
            casacore::String axisName = axNames(i);
            if (axisName == "Right Ascension") axisName = "RA";
            if (axisName == "Declination") axisName = "DEC";
            *headerEntry->mutable_value() = axisName;
            if (suffix=="1")
                coordinateTypeX = axisName;
            else if (suffix=="2")
                coordinateTypeY = axisName;
            else if (suffix=="4")
                coordinateType4 = axisName;
            // ref val = CRVAL
            headerEntry = extendedInfo->add_header_entries();
            headerEntry->set_name("CRVAL"+ suffix);
            *headerEntry->mutable_value() = fmt::format("{}", axRefVal(i));
            headerEntry->set_entry_type(EntryType::FLOAT);
            headerEntry->set_numeric_value(axRefVal(i));
            // increment = CDELT
            headerEntry = extendedInfo->add_header_entries();
            headerEntry->set_name("CDELT"+ suffix);
            *headerEntry->mutable_value() = fmt::format("{}", axInc(i));
            headerEntry->set_entry_type(EntryType::FLOAT);
            headerEntry->set_numeric_value(axInc(i));
            // ref pix = CRPIX
            headerEntry = extendedInfo->add_header_entries();
            headerEntry->set_name("CRPIX"+ suffix);
            *headerEntry->mutable_value() = fmt::format("{}", axRefPix(i));
            headerEntry->set_entry_type(EntryType::FLOAT);
            headerEntry->set_numeric_value(axRefPix(i));
            // units = CUNIT
            headerEntry = extendedInfo->add_header_entries();
            headerEntry->set_name("CUNIT"+ suffix);
            *headerEntry->mutable_value() = axUnits(i);
            headerEntry->set_entry_type(EntryType::STRING);
        }
        // cr coords
        std::string crPixels, crCoords, crDegStr, axisInc;
        if (axisSize > 1) {
            std::string crpix0(std::to_string(static_cast<int>(axRefPix(0)))),
                        crpix1(std::to_string(static_cast<int>(axRefPix(1)))),
                        cunit0(axUnits(0)),
                        cunit1(axUnits(1));
            double crval0(axRefVal(0)), cdelt0(axInc(0)),
                   crval1(axRefVal(1)), cdelt1(axInc(1));
            crPixels = fmt::format("[{}, {}]", crpix0, crpix1);
            crCoords = fmt::format("[{:.4f} {}, {:.4f} {}]", crval0, cunit0, crval1, cunit1);
            crDegStr = makeDegStr(coordinateTypeX, crval0, crval1, cunit0, cunit1);
            axisInc = fmt::format("{} {}, {} {}", cdelt0, cunit0, cdelt1, cunit1);
        }

        // depth, stokes
        bool stokesIsAxis4(true);
        if (dim<4) {
            extendedInfo->set_depth(dim > 2 ? imShape(2) : 1);
            extendedInfo->set_stokes(1);
        } else {
            transform(coordinateType4.begin(), coordinateType4.end(), coordinateType4.begin(), ::toupper);
            if (coordinateType4=="STOKES") {
                extendedInfo->set_depth(imShape(2));
                extendedInfo->set_stokes(imShape(3));
            } else {
                extendedInfo->set_depth(imShape(3));
                extendedInfo->set_stokes(imShape(2));
                stokesIsAxis4 = false;
            }
        }
        // RESTFRQ
        casacore::String returnStr;
        casacore::Quantum<casacore::Double> restFreq;
        casacore::Bool ok = imSummary.restFrequency(returnStr, restFreq);
        if (ok) {
            headerEntry = extendedInfo->add_header_entries();
            headerEntry->set_name("RESTFRQ");
            casacore::Double restFreqVal(restFreq.getValue());
            *headerEntry->mutable_value() = returnStr;
            headerEntry->set_entry_type(EntryType::FLOAT);
            headerEntry->set_numeric_value(restFreqVal);
        }
        // SPECSYS
        casacore::MFrequency::Types freqTypes;
        ok = imSummary.frequencySystem(returnStr, freqTypes);
        if (ok) {
            headerEntry = extendedInfo->add_header_entries();
            headerEntry->set_name("SPECSYS");
            *headerEntry->mutable_value() = returnStr;
            headerEntry->set_entry_type(EntryType::STRING);
            specSys = returnStr;
        }
        // telescope
        headerEntry = extendedInfo->add_header_entries();
        headerEntry->set_name("TELESCOP");
        *headerEntry->mutable_value() = imSummary.telescope();
        headerEntry->set_entry_type(EntryType::STRING);
        // observer
        headerEntry = extendedInfo->add_header_entries();
        headerEntry->set_name("OBSERVER");
        *headerEntry->mutable_value() = imSummary.observer();
        headerEntry->set_entry_type(EntryType::STRING);
        // obs date
        casacore::MEpoch epoch;
        headerEntry = extendedInfo->add_header_entries();
        headerEntry->set_name("DATE");
        *headerEntry->mutable_value() = imSummary.obsDate(epoch);
        headerEntry->set_entry_type(EntryType::STRING);

        // computed_entries
        addComputedEntries(extendedInfo, coordinateTypeX, coordinateTypeY, crPixels, crCoords, crDegStr,
            radeSys, specSys, bunit, axisInc, stokesIsAxis4);
    } catch (casacore::AipsError& err) {
        if (ccImage != nullptr)
            delete ccImage;
        message = err.getMesg().c_str();
        extInfoOK = false;
    }
    if (ccImage != nullptr)
        delete ccImage;
    return extInfoOK;
}

void FileInfoLoader::addComputedEntries(CARTA::FileInfoExtended* extendedInfo, const std::string& coordinateTypeX,
    const std::string& coordinateTypeY, const std::string& crPixels, const std::string& crCoords,
    const std::string& crDeg, const std::string& radeSys, const std::string& specSys, const std::string& bunit,
    const std::string& axisInc, const bool stokesIsAxis4) {
    // add computed_entries to extended info

    // name
    casacore::File ccfile(m_file);
    string name(ccfile.path().baseName());
    auto computedEntryName = extendedInfo->add_computed_entries();
    computedEntryName->set_name("Name");
    computedEntryName->set_value(name);
    computedEntryName->set_entry_type(EntryType::STRING);

    // shape
    string shapeString;
    int ndims(extendedInfo->dimensions()),
        nchan(extendedInfo->depth()),
        nstokes(extendedInfo->stokes());
    switch(ndims) {
        case 2:
            shapeString = fmt::format("[{}, {}]", extendedInfo->width(), extendedInfo->height());
            break;
        case 3:
            shapeString = fmt::format("[{}, {}, {}]", extendedInfo->width(), extendedInfo->height(), nchan); 
            break;
        case 4:
            if (stokesIsAxis4) {
                shapeString = fmt::format("[{}, {}, {}, {}]", extendedInfo->width(), extendedInfo->height(),
                    nchan, nstokes);
            } else {
                shapeString = fmt::format("[{}, {}, {}, {}]", extendedInfo->width(), extendedInfo->height(),
                    nstokes, nchan);
            }
            break;
    }
    auto computedEntryShape = extendedInfo->add_computed_entries();
    computedEntryShape->set_name("Shape");
    computedEntryShape->set_value(shapeString);
    computedEntryShape->set_entry_type(EntryType::STRING);

    // chan, stokes
    if (ndims>=3) {
        auto computedEntryNChan = extendedInfo->add_computed_entries();
        computedEntryNChan->set_name("Number of channels");
        computedEntryNChan->set_value(casacore::String::toString(nchan));
        computedEntryNChan->set_entry_type(EntryType::INT);
        computedEntryNChan->set_numeric_value(nchan);
    }
    if (ndims==4) {
        auto computedEntryNStokes = extendedInfo->add_computed_entries();
        computedEntryNStokes->set_name("Number of stokes");
        computedEntryNStokes->set_value(casacore::String::toString(nstokes));
        computedEntryNStokes->set_entry_type(EntryType::INT);
        computedEntryNStokes->set_numeric_value(nstokes);
    }

    if (!coordinateTypeX.empty() && !coordinateTypeY.empty()) {
        auto computedEntryCoordinateType = extendedInfo->add_computed_entries();
        computedEntryCoordinateType->set_name("Coordinate type");
        computedEntryCoordinateType->set_value(fmt::format("{}, {}", coordinateTypeX, coordinateTypeY));
        computedEntryCoordinateType->set_entry_type(EntryType::STRING);
    }

    if (!crPixels.empty()) {
        auto computedEntryCrPixels = extendedInfo->add_computed_entries();
        computedEntryCrPixels->set_name("Image reference pixels");
        computedEntryCrPixels->set_value(crPixels);
        computedEntryCrPixels->set_entry_type(EntryType::STRING);
    }

    if (!crCoords.empty()) {
        auto computedEntryCrCoords = extendedInfo->add_computed_entries();
        computedEntryCrCoords->set_name("Image reference coordinates");
        computedEntryCrCoords->set_value(crCoords);
        computedEntryCrCoords->set_entry_type(EntryType::STRING);
    }

    if (!crDeg.empty()) {
        auto computedEntryCrDeg = extendedInfo->add_computed_entries();
        computedEntryCrDeg->set_name("Image ref coords (coord type)");
        computedEntryCrDeg->set_value(crDeg);
        computedEntryCrDeg->set_entry_type(EntryType::STRING);
    }

    if (!radeSys.empty()) {
        auto computedEntryCelestialFrame = extendedInfo->add_computed_entries();
        computedEntryCelestialFrame->set_name("Celestial frame");
        computedEntryCelestialFrame->set_value(radeSys);
        computedEntryCelestialFrame->set_entry_type(EntryType::STRING);
    }

    if (!specSys.empty()) {
        auto computedEntrySpectralFrame = extendedInfo->add_computed_entries();
        computedEntrySpectralFrame->set_name("Spectral frame");
        computedEntrySpectralFrame->set_value(specSys);
        computedEntrySpectralFrame->set_entry_type(EntryType::STRING);
    }

    if (!bunit.empty()) {
        auto computedEntryPixelUnit = extendedInfo->add_computed_entries();
        computedEntryPixelUnit->set_name("Pixel unit");
        computedEntryPixelUnit->set_value(bunit);
        computedEntryPixelUnit->set_entry_type(EntryType::STRING);
    }
    if (!axisInc.empty()) {
        auto computedEntryAxisInc = extendedInfo->add_computed_entries();
        computedEntryAxisInc->set_name("Pixel increment");
        computedEntryAxisInc->set_value(axisInc);
        computedEntryAxisInc->set_entry_type(EntryType::STRING);
    }
}

