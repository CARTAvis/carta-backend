//# FileInfoLoader.h: load FileInfo and FileInfoExtended fields for all supported file types

#pragma once

#include <carta-protobuf/file_info.pb.h>
#include <casacore/casa/Arrays/IPosition.h>
#include <casacore/images/Images/ImageOpener.h>
#include <string>

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
    // computed entries
    void addShapeEntries(CARTA::FileInfoExtended* extendedInfo, const casacore::IPosition& shape,
        int chanAxis, int stokesAxis);
    void addComputedEntries(CARTA::FileInfoExtended* extInfo, const std::string& xyCoords,
        const std::string& crPixels, const std::string& crCoords, const std::string& crRaDec,
	    const std::string& radeSys, const std::string& specSys, const std::string& bunit,
	    const std::string& axisInc, const std::string& rsBeam);

    // ExtFileInfo helpers
    std::string makeValueStr(const std::string& type, double val, const std::string& unit);
    void makeRadesysStr(std::string& radeSys, const std::string& equinox);
    // get HDF5 attributes in Record, may have to get string and convert to numeric
    bool getIntAttribute(casacore::Int64& val, const casacore::Record& rec, const casacore::String& field);
    bool getDoubleAttribute(casacore::Double& val, const casacore::Record& rec, const casacore::String& field);
    void findChanStokesAxis(const casacore::IPosition& dataShape, const casacore::String& axisType1,
        const casacore::String& axisType2, const casacore::String& axisType3, const casacore::String& axisType4,
	int& chanAxis, int& stokesAxis);
    std::string unitConversion(const double value, const std::string& unit);
    std::string deg2arcsec(const double degree);
    std::string convertHz(const double hz);


    std::string m_file;
    casacore::ImageOpener::ImageTypes m_type; 
};

