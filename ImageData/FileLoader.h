#pragma once

#include <casacore/images/Images/ImageOpener.h>
#include <casacore/images/Images/ImageInterface.h>
#include <string>
#include <memory>

namespace carta {

namespace FileInfo {

struct ImageStats {
    float minVal;
    float maxVal;
    float mean;
    std::vector<float> percentiles;
    std::vector<float> percentileRanks;
    std::vector<int> histogramBins;
    int64_t nanCount;
};

enum class Data : uint32_t
{
     // Standard layouts
    XY, XYZ, XYZW,
     // Swizzled layouts
    YX, ZYX, ZYXW,
    // Statistics tables
    Stats, Stats2D, S2DMin, S2DMax, S2DMean, S2DNans, S2DHist, S2DPercent, Ranks,
    Stats3D, S3DMin, S3DMax, S3DMean, S3DNans, S3DHist, S3DPercent,
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
    using channel_stats_ref = std::vector<std::vector<FileInfo::ImageStats>>&;
    using cube_stats_ref = std::vector<FileInfo::ImageStats>&;
    
    virtual ~FileLoader() = default;

    static FileLoader* getLoader(const std::string &file);
    // return coordinates for axis types
    virtual void findCoords(int& spectralAxis, int& stokesAxis);
    // Load image statistics, if they exist, from the file into the data structures provided
    virtual void loadImageStats(channel_stats_ref channelStats, cube_stats_ref cubeStats,
        size_t nchannels, size_t nstokes, size_t ndims,
        bool loadPercentiles=false);

    // Do anything required to open the file (set up cache size, etc)
    virtual void openFile(const std::string &file, const std::string &hdu) = 0;
    // Check to see if the file has a particular HDU/group/table/etc
    virtual bool hasData(FileInfo::Data ds) const = 0;
    // Return a casacore image type representing the data stored in the
    // specified HDU/group/table/etc.
    virtual image_ref loadData(FileInfo::Data ds) = 0;
protected:
    virtual const casacore::CoordinateSystem& getCoordSystem() = 0;
};

} // namespace carta
