//# FileInfoLoader.h: load FileInfo and FileInfoExtended fields for all supported file types

#pragma once

#include <carta-protobuf/file_info.pb.h>
#include <casacore/images/Images/ImageOpener.h>
#include <casacore/casa/HDF5/HDF5Record.h>
#include <string>

class HDF5Attributes {

public:
    // HDF5Record::doReadRecord modified to not iterate through links
    // (links get HDF5Error "Could not open group XXX in parent")
    static casacore::Record doReadAttributes(hid_t groupHid);

private:
    // Read a scalar value (int, float, string) and add it to the record.
    static void readScalar (hid_t attrId, hid_t dtid, const casacore::String& name,
        casacore::RecordInterface& rec);
};

// #####################################################################

class FileInfoLoader {

public:
    FileInfoLoader(const std::string& filename);
    ~FileInfoLoader() {}

    bool fillFileInfo(CARTA::FileInfo* fileInfo);
    bool fillFileExtInfo(CARTA::FileInfoExtended* extInfo, std::string& hdu, std::string& message);

private:
    casacore::ImageOpener::ImageTypes fileType(const std::string &file);
    CARTA::FileType convertFileType(int ccImageType);

    // FileInfo
    bool getHduList(CARTA::FileInfo* fileInfo, const std::string& filename);

    // ExtFileInfo
    bool fillHdf5ExtFileInfo(CARTA::FileInfoExtended* extInfo, std::string& hdu, std::string& message);
    bool fillFITSExtFileInfo(CARTA::FileInfoExtended* extInfo, std::string& hdu, std::string& message);
    bool fillCASAExtFileInfo(CARTA::FileInfoExtended* extInfo, std::string& message);
    void addComputedEntries(CARTA::FileInfoExtended* extInfo, const std::string& coordinateTypeX,
        const std::string& coordinateTypeY, const std::string& crPixels, const std::string& crCoords,
        const std::string& crRaDec, const std::string& radeSys, const std::string& specSys,
        const std::string& bunit, const std::string& axisInc, const bool stokesIsAxis4);

    // ExtFileInfo helpers
    void makeRadesysStr(std::string& radeSys, const std::string& equinox);
    std::string makeDegStr(const std::string& xType, double crval1, double crval2, const std::string& cunit1,
        const std::string& cunit2);
    // HDF5 attributes in Record, may have to get string and convert to numeric
    bool getIntAttribute(casacore::Int64& val, const casacore::Record& rec, const casacore::String& field);
    bool getDoubleAttribute(casacore::Double& val, const casacore::Record& rec, const casacore::String& field);

    std::string m_file;
    casacore::ImageOpener::ImageTypes m_type; 
};

