#pragma once

#include <casacore/images/Images/ImageOpener.h>
#include <casacore/images/Images/ImageInterface.h>
#include <string>
#include <memory>

namespace carta {

namespace FileInfo {

enum class Data : uint32_t
{
     // Standard layouts
    XY, XYZ, XYZW,
     // Swizzled layouts
    YX, ZYX, ZYXW,
    // Statistics tables
    Stats, Stats2D, S2DMin, S2DMax, S2DMean, S2DNans, S2DHist, S2DPercent, Ranks,
};

inline casacore::ImageOpener::ImageTypes fileType(const std::string &file) {
     return casacore::ImageOpener::imageType(file);
}

inline casacore::uInt getFITShdu(const std::string &hdu) {
    // convert from string to casacore unsigned int
    casacore::uInt hdunum(0);
    if (!hdu.empty() && hdu!="0") {
        casacore::String ccHdu(hdu);
        ccHdu.fromString(hdunum, true);
    }
    return hdunum;
}

} // namespace FileInfo

class FileLoader {
public:
    using image_ref = casacore::Lattice<float>&;
    virtual ~FileLoader() = default;

    static FileLoader* getLoader(const std::string &file);

    // Do anything required to open the file (set up cache size, etc)
    virtual void openFile(const std::string &file, const std::string &hdu) = 0;
    // Check to see if the file has a particular HDU/group/table/etc
    virtual bool hasData(FileInfo::Data ds) const = 0;
    // Return a casacore image type representing the data stored in the
    // specified HDU/group/table/etc.
    virtual image_ref loadData(FileInfo::Data ds) = 0;
    // For 4D images, return which axis is Stokes; else -1
    virtual int stokesAxis() = 0;
};

} // namespace carta
