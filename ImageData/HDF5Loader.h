#pragma once

#include "FileLoader.h"
#include "HDF5Attributes.h"

#include <casacore/lattices/Lattices/HDF5Lattice.h>
#include <string>
#include <unordered_map>

namespace carta {

class HDF5Loader : public FileLoader {
public:
    HDF5Loader(const std::string &filename);
    void openFile(const std::string &file, const std::string &hdu) override;
    bool hasData(FileInfo::Data ds) const override;
    image_ref loadData(FileInfo::Data ds) override;
    const casacore::CoordinateSystem& getCoordSystem() override;
    void findCoords(int& spectralAxis, int& stokesAxis) override;
    void loadImageStats(channel_stats_ref channelStats, cube_stats_ref cubeStats,
        size_t nchannels, size_t nstokes, size_t ndims, 
        bool loadPercentiles=false) override;

private:
    static std::string dataSetToString(FileInfo::Data ds);
    
    template <typename T>
    void loadStats2DBasic(FileInfo::Data ds, channel_stats_ref channelStats,
        size_t nchannels, size_t nstokes, size_t ndims);
    
    void loadStats2DHist(channel_stats_ref channelStats,
        size_t nchannels, size_t nstokes, size_t ndims);
    
    void loadStats2DPercent(channel_stats_ref channelStats,
        size_t nchannels, size_t nstokes, size_t ndims);
    
    template <typename T>
    void loadStats3DBasic(FileInfo::Data ds, cube_stats_ref cubeStats,
        size_t nchannels, size_t nstokes, size_t ndims);
    
    void loadStats3DHist(cube_stats_ref cubeStats,
        size_t nchannels, size_t nstokes, size_t ndims);
    
    void loadStats3DPercent(cube_stats_ref cubeStats,
        size_t nchannels, size_t nstokes, size_t ndims);
    
    std::string file, hdf5Hdu;
    std::unordered_map<std::string, casacore::HDF5Lattice<float>> dataSets;
};

HDF5Loader::HDF5Loader(const std::string &filename)
    : file(filename),
      hdf5Hdu("0")
{}

void HDF5Loader::openFile(const std::string &filename, const std::string &hdu) {
    file = filename;
    hdf5Hdu = hdu;
}

bool HDF5Loader::hasData(FileInfo::Data ds) const {
    auto it = dataSets.find("DATA");
    if(it == dataSets.end()) return false;

    switch(ds) {
    case FileInfo::Data::XY:
        return it->second.shape().size() >= 2;
    case FileInfo::Data::XYZ:
        return it->second.shape().size() >= 3;
    case FileInfo::Data::XYZW:
        return it->second.shape().size() >= 4;
    default:
        auto group_ptr = it->second.group();
        std::string data = dataSetToString(ds);
        return casacore::HDF5Group::exists(*group_ptr, data);
    }
}

typename HDF5Loader::image_ref HDF5Loader::loadData(FileInfo::Data ds) {
    std::string data = dataSetToString(ds);
    if(dataSets.find(data) == dataSets.end()) {
        dataSets.emplace(data, casacore::HDF5Lattice<float>(file, data, hdf5Hdu));
    }
    return dataSets[data];
}

// This is necessary on some systems where the compiler
// cannot infer the implicit cast to the enum class's
// underlying type (e.g. MacOS, CentOS7).
struct EnumClassHash {
    template <typename T>
    using utype_t = typename std::underlying_type<T>::type;

    template <typename T>
    utype_t<T> operator()(T t) const {
        return static_cast<utype_t<T>>(t);
    }
};

std::string HDF5Loader::dataSetToString(FileInfo::Data ds) {
    static std::unordered_map<FileInfo::Data, std::string, EnumClassHash> um = {
        { FileInfo::Data::XY,         "DATA" },
        { FileInfo::Data::XYZ,        "DATA" },
        { FileInfo::Data::XYZW,       "DATA" },
        { FileInfo::Data::YX,         "Swizzled/YX" },
        { FileInfo::Data::ZYX,        "Swizzled/ZYX" },
        { FileInfo::Data::XYZW,       "Swizzled/ZYXW" },
        { FileInfo::Data::Stats,      "Statistics" },
        { FileInfo::Data::Stats2D,    "Statistics/XY" },
        { FileInfo::Data::S2DMin,     "Statistics/XY/MIN" },
        { FileInfo::Data::S2DMax,     "Statistics/XY/MAX" },
        { FileInfo::Data::S2DMean,    "Statistics/XY/MEAN" },
        { FileInfo::Data::S2DNans,    "Statistics/XY/NAN_COUNT" },
        { FileInfo::Data::S2DHist,    "Statistics/XY/HISTOGRAM" },
        { FileInfo::Data::S2DPercent, "Statistics/XY/PERCENTILES" },
        { FileInfo::Data::Stats3D,    "Statistics/XYZ" },
        { FileInfo::Data::S3DMin,     "Statistics/XYZ/MIN" },
        { FileInfo::Data::S3DMax,     "Statistics/XYZ/MAX" },
        { FileInfo::Data::S3DMean,    "Statistics/XYZ/MEAN" },
        { FileInfo::Data::S3DNans,    "Statistics/XYZ/NAN_COUNT" },
        { FileInfo::Data::S3DHist,    "Statistics/XYZ/HISTOGRAM" },
        { FileInfo::Data::S3DPercent, "Statistics/XYZ/PERCENTILES" },
        { FileInfo::Data::Ranks,      "PERCENTILE_RANKS" },
    };
    return (um.find(ds) != um.end()) ? um[ds] : "";
}

const casacore::CoordinateSystem& HDF5Loader::getCoordSystem() {
    // this does not work: 
    // (/casacore/lattices/LEL/LELCoordinates.cc : 69) Failed AlwaysAssert !coords_p.null()
    const casacore::LELImageCoord* lelImCoords =
        dynamic_cast<const casacore::LELImageCoord*>(&(loadData(FileInfo::Data::XYZW).lelCoordinates().coordinates()));
    return lelImCoords->coordinates();
}

// TODO: instead of creating lattices, create datasets directly
// TODO: helper function to create dataset and return array
// TODO: we need to use the C API to read scalar datasets for now, but we should patch casacore to handle them correctly
// TODO: switches outside loops for now; should profile to see if moving them inside adds significant overhead
// TODO: move main function and helper functions to top-level loader interface once all the HDF5-specific functions have been extracted to other functions.

template <typename T>
void HDF5Loader::loadStats2DBasic( FileInfo::Data ds, channel_stats_ref channelStats,
    size_t nchannels, size_t nstokes, size_t ndims) {
    if (hasData(ds)) {
        auto dataSet = casacore::HDF5Lattice<T>(file, dataSetToString(ds), hdf5Hdu);
        casacore::IPosition statDims = dataSet.shape();
        
        // 2D cubes
        if (ndims == 2 && statDims.size() == 0) {
            auto dSet = dataSet.array();
            T value;
            casacore::HDF5DataType dtype((T*)0);
            H5Dread(dSet->getHid(), dtype.getHidMem(), H5S_ALL, H5S_ALL, H5P_DEFAULT, &value);
            
            switch(ds) {
            case FileInfo::Data::S2DMax:
                channelStats[0][0].maxVal = value;
                break;
            case FileInfo::Data::S2DMin:
                channelStats[0][0].minVal = value;
                break;
            case FileInfo::Data::S2DMean:
                channelStats[0][0].mean = value;
                break;
            case FileInfo::Data::S2DNans:
                channelStats[0][0].nanCount = value;
                break;
            }
        } // 3D cubes
        else if (ndims == 3 && statDims.size() == 1 && statDims[0] == nchannels) {
            casacore::Array<T> data;
            dataSet.get(data, true);
            auto it = data.begin();
            
            switch(ds) {
            case FileInfo::Data::S2DMax:
                for (auto c = 0; c < nchannels; ++c) {
                    channelStats[0][c].maxVal = *it++;
                }
                break;
            case FileInfo::Data::S2DMin:
                for (auto c = 0; c < nchannels; ++c) {
                    channelStats[0][c].minVal = *it++;
                }
                break;
            case FileInfo::Data::S2DMean:
                for (auto c = 0; c < nchannels; ++c) {
                    channelStats[0][c].mean = *it++;
                }
                break;
            case FileInfo::Data::S2DNans:
                for (auto c = 0; c < nchannels; ++c) {
                    channelStats[0][c].nanCount = *it++;
                }
                break;
            }
        } // 4D cubes
        else if (ndims == 4 && statDims.size() == 2 &&
                    statDims[0] == nchannels && statDims[1] == nstokes) {
            casacore::Array<T> data;
            dataSet.get(data, true);
            auto it = data.begin();
            
            switch(ds) {
            case FileInfo::Data::S2DMax:
                for (auto s = 0; s < nstokes; s++) {
                    for (auto c = 0; c < nchannels; c++) {
                        channelStats[s][c].maxVal = *it++;
                    }
                }
                break;
            case FileInfo::Data::S2DMin:
                for (auto s = 0; s < nstokes; s++) {
                    for (auto c = 0; c < nchannels; c++) {
                        channelStats[s][c].minVal = *it++;
                    }
                }
                break;
            case FileInfo::Data::S2DMean:
                for (auto s = 0; s < nstokes; s++) {
                    for (auto c = 0; c < nchannels; c++) {
                        channelStats[s][c].mean = *it++;
                    }
                }
                break;
            case FileInfo::Data::S2DNans:
                for (auto s = 0; s < nstokes; s++) {
                    for (auto c = 0; c < nchannels; c++) {
                        channelStats[s][c].nanCount = *it++;
                    }
                }
                break;
            }
        }
    }
}

void HDF5Loader::loadStats2DHist(channel_stats_ref channelStats,
    size_t nchannels, size_t nstokes, size_t ndims) {
    if (hasData(FileInfo::Data::S2DHist)) {
        auto dataSet = casacore::HDF5Lattice<casacore::Int64>(file, dataSetToString(FileInfo::Data::S2DHist), hdf5Hdu);
        casacore::IPosition statDims = dataSet.shape();
        
        auto nbins = statDims[0];

        // 2D cubes
        if (ndims == 2 && statDims.size() == 1) {
            casacore::Array<casacore::Int64> data;
            dataSet.get(data, true);
            
            std::copy(data.begin(), data.end(), std::back_inserter(channelStats[0][0].histogramBins));
        } // 3D cubes
        else if (ndims == 3 && statDims.size() == 2 && statDims[1] == nchannels) {
            casacore::Array<casacore::Int64> data;
            dataSet.get(data, true);
            
            auto it = data.begin();
                                            
            for (auto c = 0; c < nchannels; c++) {
                channelStats[0][c].histogramBins.resize(nbins);
                for (auto b = 0; b < nbins; b++) {
                    channelStats[0][c].histogramBins[b] = *it++;
                }
            }
        } // 4D cubes
        else if (ndims == 4 && statDims.size() == 3 &&
                    statDims[1] == nchannels && statDims[2] == nstokes) {
            casacore::Array<casacore::Int64> data;
            dataSet.get(data, true);
            
            auto it = data.begin();
        
            for (auto s = 0; s < nstokes; s++) {
                for (auto c = 0; c < nchannels; c++) {
                    channelStats[s][c].histogramBins.resize(nbins);
                    for (auto b = 0; b < nbins; b++) {
                        channelStats[s][c].histogramBins[b] = *it++;
                    }
                }
            }
        }
    }
}

// TODO: untested; need to check if dimension order for matrix and cube is correct
// TODO: why not use the same method as other stats?

void HDF5Loader::loadStats2DPercent(channel_stats_ref channelStats,
    size_t nchannels, size_t nstokes, size_t ndims) {
    if (hasData(FileInfo::Data::S2DPercent) &&
        hasData(FileInfo::Data::Ranks)) {
        auto dataSetVals = casacore::HDF5Lattice<float>(file, dataSetToString(FileInfo::Data::S2DPercent), hdf5Hdu);
        auto dataSetRanks = casacore::HDF5Lattice<float>(file, dataSetToString(FileInfo::Data::Ranks), hdf5Hdu);
    
        casacore::IPosition dimsVals = dataSetVals.shape();
        casacore::IPosition dimsRanks = dataSetRanks.shape();

        auto nranks = dimsRanks[0];
        casacore::Vector<float> ranks(nranks);
        dataSetRanks.get(ranks, false);
    
        if (dimsVals[0] == nranks) {
            if (ndims == 2 && dimsVals.size() == 1) {
                casacore::Vector<float> vals(nranks);
                dataSetVals.get(vals, true);
                vals.tovector(channelStats[0][0].percentiles);
                ranks.tovector(channelStats[0][0].percentileRanks);
            }
                // 3D cubes
            else if (ndims == 3 && dimsVals.size() == 2 && dimsVals[1] == nchannels) {
                casacore::Matrix<float> vals(nranks, nchannels);
                dataSetVals.get(vals, false);

                for (auto c = 0; c < nchannels; c++) {
                    ranks.tovector(channelStats[0][c].percentileRanks);
                    channelStats[0][c].percentiles.resize(nranks);
                    for (auto r = 0; r < nranks; r++) {
                        channelStats[0][c].percentiles[r] = vals(r, c);
                    }
                }
            }
                // 4D cubes
            else if (ndims == 4 && dimsVals.size() == 3 && dimsVals[1] == nchannels && dimsVals[2] == nstokes) {
                casacore::Cube<float> vals(nranks, nchannels, nstokes);
                dataSetVals.get(vals, false);

                for (auto s = 0; s < nstokes; s++) {
                    for (auto c = 0; c < nchannels; c++) {
                        channelStats[s][c].percentiles.resize(nranks);
                        ranks.tovector(channelStats[s][c].percentileRanks);
                        for (auto r = 0; r < nranks; r++) {
                            channelStats[s][c].percentiles[r] = vals(r, c, s);
                        }
                    }
                }
            }
        }
    }
}

template <typename T>
void HDF5Loader::loadStats3DBasic(FileInfo::Data ds, cube_stats_ref cubeStats,
    size_t nchannels, size_t nstokes, size_t ndims) {
    if (hasData(ds)) {
        auto dataSet = casacore::HDF5Lattice<T>(file, dataSetToString(ds), hdf5Hdu);
        casacore::IPosition statDims = dataSet.shape();
                    
        // 3D cubes
        if (ndims == 3 && statDims.size() == 0) {
            auto dSet = dataSet.array();
            T value;
            casacore::HDF5DataType dtype((T*)0);
            H5Dread(dSet->getHid(), dtype.getHidMem(), H5S_ALL, H5S_ALL, H5P_DEFAULT, &value);
            
            switch(ds) {
            case FileInfo::Data::S3DMax:
                cubeStats[0].maxVal = value;
                break;
            case FileInfo::Data::S3DMin:
                cubeStats[0].minVal = value;
                break;
            case FileInfo::Data::S3DMean:
                cubeStats[0].mean = value;
                break;
            case FileInfo::Data::S3DNans:
                cubeStats[0].nanCount = value;
                break;
            }
        } // 4D cubes
        else if (ndims == 4 && statDims.size() == 1 && statDims[0] == nstokes) {
            casacore::Array<T> data;
            dataSet.get(data, true);
            auto it = data.begin();
            
            switch(ds) {
            case FileInfo::Data::S3DMax:
                for (auto s = 0; s < nstokes; s++) {
                    cubeStats[s].maxVal = *it++;
                }
                break;
            case FileInfo::Data::S3DMin:
                for (auto s = 0; s < nstokes; s++) {
                    cubeStats[s].minVal = *it++;
                }
                break;
            case FileInfo::Data::S3DMean:
                for (auto s = 0; s < nstokes; s++) {
                    cubeStats[s].mean = *it++;
                }
                break;
            case FileInfo::Data::S3DNans:
                for (auto s = 0; s < nstokes; s++) {
                    cubeStats[s].nanCount = *it++;
                }
                break;
            }
        }
    }
}

void HDF5Loader::loadStats3DHist(cube_stats_ref cubeStats,
    size_t nchannels, size_t nstokes, size_t ndims) {
    if (hasData(FileInfo::Data::S3DHist)) {
        auto dataSet = casacore::HDF5Lattice<casacore::Int64>(file, dataSetToString(FileInfo::Data::S3DHist), hdf5Hdu);
        casacore::IPosition statDims = dataSet.shape();
        auto nbins = statDims[0];
        
        // 3D cubes
        if (ndims == 3 && statDims.size() == 1) {
            casacore::Array<casacore::Int64> data;
            dataSet.get(data, true);
            std::copy(data.begin(), data.end(), std::back_inserter(cubeStats[0].histogramBins));
        } // 4D cubes
        else if (ndims == 4 && statDims.size() == 2 && statDims[1] == nstokes) {
            casacore::Array<casacore::Int64> data;
            dataSet.get(data, true);
                        
            auto it = data.begin();
            
            for (auto s = 0; s < nstokes; s++) {
                cubeStats[s].histogramBins.resize(nbins);
                for (auto b = 0; b < nbins; b++) {
                    cubeStats[s].histogramBins[b] = *it++;
                }
            }
        }
    }
}

// TODO: untested; need to check if dimension order for matrix and cube is correct
// TODO: why not use the same method as other stats?

void HDF5Loader::loadStats3DPercent(cube_stats_ref cubeStats,
    size_t nchannels, size_t nstokes, size_t ndims) {
    if (hasData(FileInfo::Data::S3DPercent) &&
        hasData(FileInfo::Data::Ranks)) {
        auto dataSetVals = casacore::HDF5Lattice<float>(file, dataSetToString(FileInfo::Data::S3DPercent), hdf5Hdu);
        auto dataSetRanks = casacore::HDF5Lattice<float>(file, dataSetToString(FileInfo::Data::Ranks), hdf5Hdu);

        casacore::IPosition dimsVals = dataSetVals.shape();
        casacore::IPosition dimsRanks = dataSetRanks.shape();

        auto nranks = dimsRanks[0];
        casacore::Vector<float> ranks(nranks);
        dataSetRanks.get(ranks, false);
    
        if (dimsVals[0] == nranks) {
            // 3D cubes
            if (ndims == 3 && dimsVals.size() == 1) {
                casacore::Vector<float> vals(nranks);
                dataSetVals.get(vals, true);
                vals.tovector(cubeStats[0].percentiles);
                ranks.tovector(cubeStats[0].percentileRanks);
            }
            // 4D cubes
            else if (ndims == 4 && dimsVals.size() == 2 && dimsVals[1] == nstokes ) {
                casacore::Matrix<float> vals(nranks, nstokes);
                dataSetVals.get(vals, false);

                for (auto s = 0; s < nstokes; s++) {
                    cubeStats[s].percentiles.resize(nranks);
                    ranks.tovector(cubeStats[s].percentileRanks);
                    for (auto r = 0; r < nranks; r++) {
                        cubeStats[s].percentiles[r] = vals(r, s);
                    }
                }
            }
        }
    }
}

void HDF5Loader::loadImageStats(channel_stats_ref channelStats, cube_stats_ref cubeStats,
    size_t nchannels, size_t nstokes, size_t ndims, bool loadPercentiles) {
    // load channel stats for entire image (all channels and stokes) from header
    // channelStats[stokes][chan]
    // cubeStats[stokes]

    channelStats.resize(nstokes);
    for (auto i = 0; i < nstokes; i++) {
        channelStats[i].resize(nchannels);
    }
    cubeStats.resize(nstokes);
    
    //TODO: Support multiple HDUs
    if (hasData(FileInfo::Data::Stats) && hasData(FileInfo::Data::Stats2D)) {
        
        loadStats2DBasic<casacore::Float>(FileInfo::Data::S2DMax, channelStats, nchannels, nstokes, ndims);
        loadStats2DBasic<casacore::Float>(FileInfo::Data::S2DMin, channelStats, nchannels, nstokes, ndims);
        loadStats2DBasic<casacore::Float>(FileInfo::Data::S2DMean, channelStats, nchannels, nstokes, ndims);
        loadStats2DBasic<casacore::Int64>(FileInfo::Data::S2DNans, channelStats, nchannels, nstokes, ndims);

        loadStats2DHist(channelStats, nchannels, nstokes, ndims);
        
        if (loadPercentiles) {
            loadStats2DPercent(channelStats, nchannels, nstokes, ndims);
        }
    }
    
    if (hasData(FileInfo::Data::Stats) && hasData(FileInfo::Data::Stats3D)) {        
        loadStats3DBasic<casacore::Float>(FileInfo::Data::S3DMax, cubeStats, nchannels, nstokes, ndims);       
        loadStats3DBasic<casacore::Float>(FileInfo::Data::S3DMin, cubeStats, nchannels, nstokes, ndims);      
        loadStats3DBasic<casacore::Float>(FileInfo::Data::S3DMean, cubeStats, nchannels, nstokes, ndims);     
        loadStats3DBasic<casacore::Int64>(FileInfo::Data::S3DNans, cubeStats, nchannels, nstokes, ndims);

        loadStats3DHist(cubeStats, nchannels, nstokes, ndims);
        
        if (loadPercentiles) {
            loadStats3DPercent(cubeStats, nchannels, nstokes, ndims);
        }
    }
}

void HDF5Loader::findCoords(int& spectralAxis, int& stokesAxis) {
    // find spectral and stokes axis in 4D image; cannot use CoordinateSystem!
    // load attributes
    casacore::HDF5File hdfFile(file);
    casacore::HDF5Group hdfGroup(hdfFile, hdf5Hdu, true);
    casacore::Record attributes = HDF5Attributes::doReadAttributes(hdfGroup.getHid());
    hdfGroup.close();
    hdfFile.close();
    if (attributes.empty()) { // use defaults
        spectralAxis = 2;
        stokesAxis = 3;
	return;
    }

    casacore::String cType3, cType4;
    if (attributes.isDefined("CTYPE3")) {
        cType3 = attributes.asString("CTYPE3");
	cType3.upcase();
    }
    if (attributes.isDefined("CTYPE4")) {
        cType4 = attributes.asString("CTYPE4");
	cType4.upcase();
    }
    // find spectral axis
    if ((cType3.startsWith("FREQ") || cType3.startsWith("VRAD") || cType3.startsWith("VELO")))
        spectralAxis = 2;
    else if ((cType4.startsWith("FREQ") || cType4.startsWith("VRAD") || cType4.startsWith("VELO")))
        spectralAxis = 3;
    else
        spectralAxis = -1;
    // find stokes axis
    if (cType3 == "STOKES")
        stokesAxis = 2;
    else if (cType4 == "STOKES")
        stokesAxis = 3;
    else 
        stokesAxis = -1;
    // make assumptions if both not found
    if (spectralAxis < 0) { // not found
        if (stokesAxis < 0) {  // not found, use defaults
            spectralAxis = 2;
            stokesAxis = 3;
        } else { // stokes found, set chan to other one
            if (stokesAxis==2) spectralAxis = 3;
            else spectralAxis = 2;
        }
    } else {  // chan found, set stokes to other one
        if (spectralAxis == 2) stokesAxis = 3;
        else stokesAxis = 2;
    } 
}

} // namespace carta
