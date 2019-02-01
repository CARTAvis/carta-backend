//# FileInfoLoader.h: load FileInfo and FileInfoExtended fields for all supported file types

#pragma once

#include <carta-protobuf/file_info.pb.h>
#include <casacore/casa/Arrays/IPosition.h>
#include <casacore/fits/FITS/FITSError.h>
#include <casacore/images/Images/ImageOpener.h>
#include <casacore/measures/Measures/MFrequency.h>
#include <casacore/measures/Measures/MDirection.h>
#include <string>

// #####################################################################

inline void fileInfoFitsErrHandler(const char *errMessage, casacore::FITSError::ErrorLevel severity) {
    if (severity > casacore::FITSError::WARN)
        std::cout << errMessage << std::endl;
}

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

    // Computed entries
    void addComputedEntries(CARTA::FileInfoExtended* extInfo, const std::string& xyCoords,
        const std::string& crPixels, const std::string& crCoords, const std::string& crRaDec,
        const std::string& radeSys, const std::string& specSys, const std::string& bunit,
        const std::string& axisInc, const std::string& rsBeam);
    void addShapeEntries(CARTA::FileInfoExtended* extendedInfo, const casacore::IPosition& shape,
        int chanAxis, int stokesAxis);
    void findChanStokesAxis(const casacore::IPosition& dataShape, const casacore::String& axisType1,
        const casacore::String& axisType2, const casacore::String& axisType3, const casacore::String& axisType4,
        int& chanAxis, int& stokesAxis);

    // casacore::Record attributes
    bool getIntAttribute(casacore::Int64& val, const casacore::Record& rec, const casacore::String& field);
    bool getDoubleAttribute(casacore::Double& val, const casacore::Record& rec, const casacore::String& field);

    // FITS keyword conversion
    void convertAxisName(std::string& axisName, std::string& projection, casacore::MDirection::Types type);
    void makeRadesysStr(std::string& radeSys, const std::string& equinox);
    void getRadesysFromEquinox(std::string& radeSys, std::string& equinox);
    bool convertSpecsysToFITS(std::string& specsys, casacore::MFrequency::Types type);
    void convertAxisNameToFITS(casacore::String& axisName, casacore::String& projection);

    // Unit conversion
    std::string unitConversion(const double value, const std::string& unit);
    double rad2deg(const double rad);
    std::string deg2arcsec(const double degree);
    std::string convertHz(const double hz);
    std::string makeValueStr(const std::string& type, double val, const std::string& unit); // convert MVAngle to string 

    std::string m_file;
    casacore::ImageOpener::ImageTypes m_type; 
};

