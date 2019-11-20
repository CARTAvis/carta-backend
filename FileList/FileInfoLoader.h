//# FileInfoLoader.h: load FileInfo and FileInfoExtended fields for all supported file types

#ifndef CARTA_BACKEND__FILELIST_FILEINFOLOADER_H_
#define CARTA_BACKEND__FILELIST_FILEINFOLOADER_H_

#include <string>

#include <casacore/casa/Arrays/IPosition.h>
#include <casacore/images/Images/ImageOpener.h>

#include <carta-protobuf/file_info.pb.h>

class FileInfoLoader {
public:
    static FileInfoLoader* GetInfoLoader(const std::string& filename);

    virtual CARTA::FileType GetCartaFileType() = 0;
    bool FillFileInfo(CARTA::FileInfo* file_info);
    bool FillFileExtInfo(CARTA::FileInfoExtended* ext_info, std::string& hdu, std::string& message);

protected:
    inline std::string GetFilename() {
        return _filename;
    }
    virtual bool GetHduList(CARTA::FileInfo* file_info, const std::string& abs_filename);
    virtual bool FillExtFileInfo(CARTA::FileInfoExtended* ext_info, std::string& hdu, std::string& message) = 0;

    void AddComputedEntries(CARTA::FileInfoExtended* ext_info, const std::string& xy_coords, const std::string& cr_pixels,
        const std::string& cr_coords, const std::string& cr_ra_dec, const std::string& rade_sys, const std::string& spec_sys,
        const std::string& bunit, const std::string& axis_inc, const std::string& rs_beam);
    void AddShapeEntries(CARTA::FileInfoExtended* extended_info, const casacore::IPosition& shape, int chan_axis, int stokes_axis);
    void FindChanStokesAxis(const casacore::IPosition& data_shape, const casacore::String& axis_type_1, const casacore::String& axis_type_2,
        const casacore::String& axis_type_3, const casacore::String& axis_type_4, int& chan_axis, int& stokes_axis);

    void MakeRadeSysStr(std::string& rade_sys, const std::string& equinox);
    std::string MakeValueStr(const std::string& type, double val, const std::string& unit); // convert MVAngle to string

    // Unit conversion
    std::string UnitConversion(const double value, const std::string& unit);
    double Rad2Deg(const double rad);
    std::string Deg2Arcsec(const double degree);
    std::string ConvertHz(const double hz);

    std::string _filename;
};

#endif // CARTA_BACKEND__FILELIST_FILEINFOLOADER_H_
