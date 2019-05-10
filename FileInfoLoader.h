//# FileInfoLoader.h: load FileInfo and FileInfoExtended fields for all supported file types

#ifndef CARTA_BACKEND__FILEINFOLOADER_H_
#define CARTA_BACKEND__FILEINFOLOADER_H_

#include <string>

#include <casacore/casa/Arrays/IPosition.h>
#include <casacore/fits/FITS/FITSError.h>
#include <casacore/images/Images/ImageOpener.h>
#include <casacore/measures/Measures/MDirection.h>
#include <casacore/measures/Measures/MFrequency.h>

#include <carta-protobuf/file_info.pb.h>

// #####################################################################

inline void FileInfoFitsErrHandler(const char* err_message, casacore::FITSError::ErrorLevel severity) {
    if (severity > casacore::FITSError::WARN)
        std::cout << err_message << std::endl;
}

class FileInfoLoader {
public:
    FileInfoLoader(const std::string& filename);

    bool FillFileInfo(CARTA::FileInfo* file_info);
    bool FillFileExtInfo(CARTA::FileInfoExtended* ext_info, std::string& hdu, std::string& message);

private:
    casacore::ImageOpener::ImageTypes FileType(const std::string& file);
    CARTA::FileType ConvertFileType(int cc_image_type);

    // FileInfo
    bool GetHduList(CARTA::FileInfo* file_info, const std::string& filename);

    // ExtFileInfo
    bool FillHdf5ExtFileInfo(CARTA::FileInfoExtended* ext_info, std::string& hdu, std::string& message);
    bool FillFitsExtFileInfo(CARTA::FileInfoExtended* ext_info, std::string& hdu, std::string& message);
    bool FillCasaExtFileInfo(CARTA::FileInfoExtended* ext_info, std::string& message);

    // Computed entries
    void AddComputedEntries(CARTA::FileInfoExtended* ext_info, const std::string& xy_coords, const std::string& cr_pixels,
        const std::string& cr_coords, const std::string& cr_ra_dec, const std::string& rade_sys, const std::string& spec_sys,
        const std::string& bunit, const std::string& axis_inc, const std::string& rs_beam);
    void AddShapeEntries(CARTA::FileInfoExtended* extended_info, const casacore::IPosition& shape, int chan_axis, int stokes_axis);
    void FindChanStokesAxis(const casacore::IPosition& data_shape, const casacore::String& axis_type_1, const casacore::String& axis_type_2,
        const casacore::String& axis_type_3, const casacore::String& axis_type_4, int& chan_axis, int& stokes_axis);

    // casacore::Record attributes
    // TODO: what are these for?
    bool getIntAttribute(casacore::Int64& val, const casacore::Record& rec, const casacore::String& field);
    bool getDoubleAttribute(casacore::Double& val, const casacore::Record& rec, const casacore::String& field);

    // FITS keyword conversion
    void ConvertAxisName(std::string& axis_name, std::string& projection, casacore::MDirection::Types type);
    void MakeRadeSysStr(std::string& rade_sys, const std::string& equinox);
    void GetRadeSysFromEquinox(std::string& rade_sys, std::string& equinox);
    bool ConvertSpecSysToFits(std::string& spec_sys, casacore::MFrequency::Types type);

    // Unit conversion
    std::string UnitConversion(const double value, const std::string& unit);
    double Rad2Deg(const double rad);
    std::string Deg2Arcsec(const double degree);
    std::string ConvertHz(const double hz);
    std::string MakeValueStr(const std::string& type, double val, const std::string& unit); // convert MVAngle to string

    std::string _m_file;
    casacore::ImageOpener::ImageTypes _m_type;
};

#endif // CARTA_BACKEND__FILEINFOLOADER_H_
