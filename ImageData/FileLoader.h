#ifndef CARTA_BACKEND_IMAGEDATA_FILELOADER_H_
#define CARTA_BACKEND_IMAGEDATA_FILELOADER_H_

#include <memory>
#include <string>

#include <casacore/images/Images/ImageInterface.h>
#include <casacore/images/Images/ImageOpener.h>

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
    bool valid;
};

enum class Data : uint32_t {
    // Main dataset
    Image,
    // Possible aliases to main dataset
    XY,
    XYZ,
    XYZW,
    // Possible swizzled datasets
    YX,
    ZYX,
    ZYXW,
    // Alias to swizzled dataset
    Swizzled,
    // Statistics tables
    Stats,
    Ranks,
    Stats2D,
    S2DMin,
    S2DMax,
    S2DMean,
    S2DNans,
    S2DHist,
    S2DPercent,
    Stats3D,
    S3DMin,
    S3DMax,
    S3DMean,
    S3DNans,
    S3DHist,
    S3DPercent,
    // Mask
    Mask
};

inline casacore::ImageOpener::ImageTypes fileType(const std::string& file) {
    return casacore::ImageOpener::imageType(file);
}

inline casacore::uInt getFITShdu(const std::string& hdu) {
    // convert from string to casacore unsigned int
    casacore::uInt hdunum(0);
    if (!hdu.empty() && hdu != "0") {
        casacore::String ccHdu(hdu);
        ccHdu.fromString(hdunum, true);
    }
    return hdunum;
}

} // namespace FileInfo

class FileLoader {
public:
    using image_ref = casacore::Lattice<float>&;
    using ipos = casacore::IPosition;

    virtual ~FileLoader() = default;

    static FileLoader* getLoader(const std::string& file);
    // return coordinates for axis types
    virtual void findCoords(int& spectralAxis, int& stokesAxis);

    // get shape and axis information from image
    virtual bool findShape(ipos& shape, size_t& nchannels, size_t& nstokes, int& spectralAxis, int& stokesAxis);

    // Load image statistics, if they exist, from the file
    virtual void loadImageStats(bool loadPercentiles = false);
    // Retrieve stats for a particular channel or all channels
    virtual FileInfo::ImageStats& getImageStats(int currStokes, int channel);

    // Do anything required to open the file (set up cache size, etc)
    virtual void openFile(const std::string& file, const std::string& hdu) = 0;
    // Check to see if the file has a particular HDU/group/table/etc
    virtual bool hasData(FileInfo::Data ds) const = 0;
    // Return a casacore image type representing the data stored in the
    // specified HDU/group/table/etc.
    virtual image_ref loadData(FileInfo::Data ds) = 0;
    virtual bool getPixelMaskSlice(casacore::Array<bool>& mask, const casacore::Slicer& slicer) = 0;
    virtual bool getCursorSpectralData(std::vector<float>& data, int stokes, int cursorX, int cursorY);

protected:
    virtual const casacore::CoordinateSystem& getCoordSystem() = 0;

    // Dimension values used by stats functions
    size_t nchannels, nstokes, ndims;
    // Storage for channel and cube statistics
    std::vector<std::vector<carta::FileInfo::ImageStats>> channelStats;
    std::vector<carta::FileInfo::ImageStats> cubeStats;
    // Return the shape of the specified stats dataset
    virtual const ipos getStatsDataShape(FileInfo::Data ds);
    // Return stats data as a casacore::Array of type casacore::Float or casacore::Int64
    virtual casacore::ArrayBase* getStatsData(FileInfo::Data ds);
    // Functions for loading individual types of statistics
    virtual void loadStats2DBasic(FileInfo::Data ds);
    virtual void loadStats2DHist();
    virtual void loadStats2DPercent();
    virtual void loadStats3DBasic(FileInfo::Data ds);
    virtual void loadStats3DHist();
    virtual void loadStats3DPercent();
};

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_FILELOADER_H_
