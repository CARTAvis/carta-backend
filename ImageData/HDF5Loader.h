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
    void loadImageStats(
        std::vector<std::vector<FileInfo::ImageStats>>& channelStats,
        std::vector<FileInfo::ImageStats>& cubeStats,
        bool loadPercentiles=false
    ) override;

private:
    static std::string dataSetToString(FileInfo::Data ds);

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
    switch(ds) {
    case FileInfo::Data::XY:
    case FileInfo::Data::XYZ:
    case FileInfo::Data::XYZW:
        int ndims;
        switch(ds) {
        case FileInfo::Data::XY:
            ndims = 2;
            break;
        case FileInfo::Data::XYZ:
            ndims = 3;
            break;
        case FileInfo::Data::XYZW:
            ndims = 4;
            break;
        }
        auto it = dataSets.find("DATA");
        if(it == dataSets.end()) return false;
        return it->second.shape().size() >= ndims;
    default:
        // TODO this is definitely broken.
        return casacore::HDF5Group::exists(*group_ptr, dataSetToString(ds)); // TODO: how to get HID from hdu group name?
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

// TODO: NEEDS MAJOR SURGERY
// TODO: we need to consider how much of this should be generic code in the top-level loader
// TODO: but first the boilerplate needs to be refactored.
void HDF5Loader::loadImageStats(
    std::vector<std::vector<FileInfo::ImageStats>>& channelStats,
    std::vector<FileInfo::ImageStats>& cubeStats,
    bool loadPercentiles
) {
    // load channel stats for entire image (all channels and stokes) from header
    // channelStats[stokes][chan]
    // cubeStats[stokes]
    if (!valid) {
        return false;
    }

    size_t depth(nchannels());
    size_t nstokes(stokesAxis>=0 ? imageShape(stokesAxis) : 1);
    channelStats.resize(nstokes);
    for (auto i = 0; i < nstokes; i++) {
        channelStats[i].resize(depth);
    }
    cubeStats.resize(nstokes);
    size_t ndims = imageShape.size();
    
    //TODO: Support multiple HDUs
    if (hasData(FileInfo::Data::Stats) && hasData(FileInfo::Data::Stats2D)) {
        
        if (hasData(FileInfo::Data::S2DMax)) {
            auto &dataSet = loadData(FileInfo::Data::S2DMax);
            casacore::IPosition statDims = dataSet.shape();

            // 2D cubes
            if (ndims == 2 && statDims.size() == 0) {
                casacore::Array<float> data;
                dataSet.get(data, true);
                auto it = data.begin();
                channelStats[0][0].maxVal = *it;
            } // 3D cubes
            else if (ndims == 3 && statDims.size() == 1 && statDims[0] == depth) {
                casacore::Array<float> data;
                dataSet.get(data, true);
                auto it = data.begin();
                for (auto i = 0; i < depth; ++i) {
                    channelStats[0][i].maxVal = *it++;
                }
            } // 4D cubes
            else if (ndims == 4 && statDims.size() == 2 &&
                     statDims[0] == nstokes && statDims[1] == depth) {
                casacore::Array<float> data;
                dataSet.get(data, true);
                auto it = data.begin();
                for (auto i = 0; i < nstokes; i++) {
                    for (auto j = 0; j < depth; j++) {
                        channelStats[i][j].maxVal = *it++;
                    }
                }
            }
        }

        if (hasData(FileInfo::Data::S2DMin)) {
            auto &dataSet = loadData(FileInfo::Data::S2DMin);
            casacore::IPosition statDims = dataSet.shape();

            // 2D cubes
            if (ndims == 2 && statDims.size() == 0) {
                casacore::Array<float> data;
                dataSet.get(data, true);
                auto it = data.begin();
                channelStats[0][0].minVal = *it;
            } // 3D cubes
            else if (ndims == 3 && statDims.size() == 1 && statDims[0] == depth) {
                casacore::Array<float> data;
                dataSet.get(data, true);
                auto it = data.begin();
                for (auto i = 0; i < depth; ++i) {
                    channelStats[0][i].minVal = *it++;
                }
            } // 4D cubes
            else if (ndims == 4 && statDims.size() == 2 &&
                     statDims[0] == nstokes && statDims[1] == depth) {
                casacore::Array<float> data;
                dataSet.get(data, true);
                auto it = data.begin();
                for (auto i = 0; i < nstokes; i++) {
                    for (auto j = 0; j < depth; j++) {
                        channelStats[i][j].minVal = *it++;
                    }
                }
            }
        }

        if (hasData(FileInfo::Data::S2DMean)) {
            auto &dataSet = loadData(FileInfo::Data::S2DMean);
            casacore::IPosition statDims = dataSet.shape();

            // 2D cubes
            if (ndims == 2 && statDims.size() == 0) {
                casacore::Array<float> data;
                dataSet.get(data, true);
                auto it = data.begin();
                channelStats[0][0].mean = *it;
            } // 3D cubes
            else if (ndims == 3 && statDims.size() == 1 && statDims[0] == depth) {
                casacore::Array<float> data;
                dataSet.get(data, true);
                auto it = data.begin();
                for (auto i = 0; i < depth; ++i) {
                    channelStats[0][i].mean = *it++;
                }
            } // 4D cubes
            else if (ndims == 4 && statDims.size() == 2 &&
                     statDims[0] == nstokes && statDims[1] == depth) {
                casacore::Array<float> data;
                dataSet.get(data, true);
                auto it = data.begin();
                for (auto i = 0; i < nstokes; i++) {
                    for (auto j = 0; j < depth; j++) {
                        channelStats[i][j].mean = *it++;
                    }
                }
            }
        }

        if (hasData(FileInfo::Data::S2DNans)) {
            // TODO: a temporary workaround, because the datasets are assumed to be floats
            // TODO: we should either do this for all the stats and leave them out of the map, or see if we can make the map polymorphic
            auto dataSet = casacore::HDF5Lattice<int>(file, dataSetToString(FileInfo::Data::S2DNans), hdf5Hdu);
            casacore::IPosition statDims = dataSet.shape();

            // 2D cubes
            if (ndims == 2 && statDims.size() == 0) {
                casacore::Array<int64_t> data;
                dataSet.get(data, true);
                auto it = data.begin();
                channelStats[0][0].nanCount = *it;
            } // 3D cubes
            else if (ndims == 3 && statDims.size() == 1 && statDims[0] == depth) {
                casacore::Array<int64_t> data;
                dataSet.get(data, true);
                auto it = data.begin();
                for (auto i = 0; i < depth; ++i) {
                    channelStats[0][i].nanCount = *it++;
                }
            } // 4D cubes
            else if (ndims == 4 && statDims.size() == 2 &&
                     statDims[0] == nstokes && statDims[1] == depth) {
                casacore::Array<int64_t> data;
                dataSet.get(data, true);
                auto it = data.begin();
                for (auto i = 0; i < nstokes; i++) {
                    for (auto j = 0; j < depth; j++) {
                        channelStats[i][j].nanCount = *it++;
                    }
                }
            }
        }

        if (hasData(FileInfo::Data::S2DHist)) {
            auto &dataSet = loadData(FileInfo::Data::S2DHist);
            casacore::IPosition statDims = dataSet.shape();
            auto numBins = statDims[2];

            // 2D cubes
            if (ndims == 2 && statDims.size() == 0) {
                casacore::Array<float> data;
                dataSet.get(data, true);
                auto it = data.begin();
                std::copy(data.begin(), data.end(),
                          std::back_inserter(channelStats[0][0].histogramBins));
            } // 3D cubes
            else if (ndims == 3 && statDims.size() == 1 && statDims[0] == depth) {
                casacore::Array<float> data;
                dataSet.get(data, true);
                auto it = data.begin();
                for (auto i = 0; i < depth; ++i) {
                    channelStats[0][i].histogramBins.resize(numBins);
                    for (auto j = 0; j < numBins; j++) {
                        channelStats[0][i].histogramBins[j] = *it++;
                    }
                }
            } // 4D cubes
            else if (ndims == 4 && statDims.size() == 2 &&
                     statDims[0] == nstokes && statDims[1] == depth) {
                casacore::Array<float> data;
                dataSet.get(data, true);
                auto it = data.begin();
                for (auto i = 0; i < nstokes; i++) {
                    for (auto j = 0; j < depth; j++) {
                        auto& stats = channelStats[i][j];
                        stats.histogramBins.resize(numBins);
                        for (auto k = 0; k < numBins; k++) {
                            stats.histogramBins[k] = *it++;
                        }
                    }
                }
            }
        }

        if (loadPercentiles) {
            if (hasData(FileInfo::Data::S2DPercent) &&
                hasData(FileInfo::Data::Ranks)) {
                auto &dataSetPercentiles = loadData(FileInfo::Data::S2DPercent);
                auto &dataSetPercentilesRank = loadData(FileInfo::Data::Ranks);

                casacore::IPosition dimsPercentiles = dataSetPercentiles.shape();
                casacore::IPosition dimsRanks = dataSetPercentilesRank.shape();

                auto numRanks = dimsRanks[0];
                casacore::Vector<float> ranks(numRanks);
                dataSetPercentilesRank.get(ranks, false);

                if (ndims == 2 && dimsPercentiles.size() == 1 && dimsPercentiles[0] == numRanks) {
                    casacore::Vector<float> vals(numRanks);
                    dataSetPercentiles.get(vals, true);
                    vals.tovector(channelStats[0][0].percentiles);
                    ranks.tovector(channelStats[0][0].percentileRanks);
                }
                    // 3D cubes
                else if (ndims == 3 && dimsPercentiles.size() == 2 && dimsPercentiles[0] == depth &&
                         dimsPercentiles[1] == numRanks) {
                    casacore::Matrix<float> vals(depth, numRanks);
                    dataSetPercentiles.get(vals, false);

                    for (auto i = 0; i < depth; i++) {
                        ranks.tovector(channelStats[0][i].percentileRanks);
                        channelStats[0][i].percentiles.resize(numRanks);
                        for (auto j = 0; j < numRanks; j++) {
                            channelStats[0][i].percentiles[j] = vals(i,j);
                        }
                    }
                }
                    // 4D cubes
                else if (ndims == 4 && dimsPercentiles.size() == 3 && dimsPercentiles[0] == nstokes &&
                         dimsPercentiles[1] == depth && dimsPercentiles[2] == numRanks) {
                    casacore::Cube<float> vals(nstokes, depth, numRanks);
                    dataSetPercentiles.get(vals, false);

                    for (auto i = 0; i < nstokes; i++) {
                        for (auto j = 0; j < depth; j++) {
                            auto& stats = channelStats[i][j];
                            stats.percentiles.resize(numRanks);
                            for (auto k = 0; k < numRanks; k++) {
                                stats.percentiles[k] = vals(i,j,k);
                            }
                            ranks.tovector(stats.percentileRanks);
                        }
                    }
                }
            }
        }
    }
    
    if (hasData(FileInfo::Data::Stats) && hasData(FileInfo::Data::Stats3D)) {        
        if (hasData(FileInfo::Data::S3DMax)) {
            auto &dataSet = loadData(FileInfo::Data::S3DMax);
            casacore::IPosition statDims = dataSet.shape();

            // 3D cubes
            if (ndims == 3 && statDims.size() == 0) {
                casacore::Array<float> data;
                dataSet.get(data, true);
                auto it = data.begin();
                cubeStats[0].maxVal = *it;
            } // 4D cubes
            else if (ndims == 4 && statDims.size() == 1 && statDims[0] == nstokes) {
                casacore::Array<float> data;
                dataSet.get(data, true);
                auto it = data.begin();
                for (auto i = 0; i < nstokes; i++) {
                    cubeStats[i].maxVal = *it++;
                }
            }
        }

        if (hasData(FileInfo::Data::S3DMin)) {
            auto &dataSet = loadData(FileInfo::Data::S3DMin);
            casacore::IPosition statDims = dataSet.shape();

            // 3D cubes
            if (ndims == 3 && statDims.size() == 0) {
                casacore::Array<float> data;
                dataSet.get(data, true);
                auto it = data.begin();
                cubeStats[0].minVal = *it;
            } // 4D cubes
            else if (ndims == 4 && statDims.size() == 1 && statDims[0] == nstokes) {
                casacore::Array<float> data;
                dataSet.get(data, true);
                auto it = data.begin();
                for (auto i = 0; i < nstokes; i++) {
                    cubeStats[i].minVal = *it++;
                }
            }
        }

        if (hasData(FileInfo::Data::S3DMean)) {
            auto &dataSet = loadData(FileInfo::Data::S3DMean);
            casacore::IPosition statDims = dataSet.shape();

            // 3D cubes
            if (ndims == 3 && statDims.size() == 0) {
                casacore::Array<float> data;
                dataSet.get(data, true);
                auto it = data.begin();
                cubeStats[0].mean = *it;
            } // 4D cubes
            else if (ndims == 4 && statDims.size() == 1 && statDims[0] == nstokes) {
                casacore::Array<float> data;
                dataSet.get(data, true);
                auto it = data.begin();
                for (auto i = 0; i < nstokes; i++) {
                    cubeStats[i].mean = *it++;
                }
            }
        }

        if (hasData(FileInfo::Data::S3DNans)) {
            // TODO: a temporary workaround, because the datasets are assumed to be floats
            // TODO: we should either do this for all the stats and leave them out of the map, or see if we can make the map polymorphic
            auto dataSet = casacore::HDF5Lattice<int>(file, dataSetToString(FileInfo::Data::S3DNans), hdf5Hdu);
            casacore::IPosition statDims = dataSet.shape();

            // 3D cubes
            if (ndims == 3 && statDims.size() == 0) {
                casacore::Array<int64_t> data;
                dataSet.get(data, true);
                auto it = data.begin();
                cubeStats[0].nanCount = *it;
            } // 4D cubes
            else if (ndims == 4 && statDims.size() == 1 && statDims[0] == nstokes) {
                casacore::Array<int64_t> data;
                dataSet.get(data, true);
                auto it = data.begin();
                for (auto i = 0; i < nstokes; i++) {
                    cubeStats[i].nanCount = *it++;
                }
            }
        }

        if (hasData(FileInfo::Data::S3DHist)) {
            auto &dataSet = loadData(FileInfo::Data::S3DHist);
            casacore::IPosition statDims = dataSet.shape();
            auto numBins = statDims[2];
            
            // 3D cubes
            if (ndims == 3 && statDims.size() == 0) {
                casacore::Array<float> data;
                dataSet.get(data, true);
                auto it = data.begin();
                std::copy(data.begin(), data.end(),
                          std::back_inserter(cubeStats[0].histogramBins));
            } // 4D cubes
            else if (ndims == 4 && statDims.size() == 1 &&
                     statDims[0] == nstokes) {
                casacore::Array<float> data;
                dataSet.get(data, true);
                auto it = data.begin();
                for (auto i = 0; i < nstokes; i++) {
                    cubeStats[i].histogramBins.resize(numBins);
                    for (auto k = 0; k < numBins; k++) {
                        cubeStats[i].histogramBins[k] = *it++;
                    }
                }
            }
        }

        if (loadPercentiles) {
            if (hasData(FileInfo::Data::S3DPercent) &&
                hasData(FileInfo::Data::Ranks)) {
                auto &dataSetPercentiles = loadData(FileInfo::Data::S3DPercent);
                auto &dataSetPercentilesRank = loadData(FileInfo::Data::Ranks);

                casacore::IPosition dimsPercentiles = dataSetPercentiles.shape();
                casacore::IPosition dimsRanks = dataSetPercentilesRank.shape();

                auto numRanks = dimsRanks[0];
                casacore::Vector<float> ranks(numRanks);
                dataSetPercentilesRank.get(ranks, false);
            
                // 3D cubes
                if (ndims == 3 && dimsPercentiles.size() == 1 && dimsPercentiles[0] == numRanks) {
                    casacore::Vector<float> vals(numRanks);
                    dataSetPercentiles.get(vals, true);
                    vals.tovector(cubeStats[0].percentiles);
                    ranks.tovector(cubeStats[0].percentileRanks);
                }
                // 4D cubes
                else if (ndims == 4 && dimsPercentiles.size() == 2 && dimsPercentiles[0] == nstokes && 
                         dimsPercentiles[1] == numRanks) {
                    
                    casacore::Matrix<float> vals(nstokes, numRanks);
                    dataSetPercentiles.get(vals, false);

                    for (auto i = 0; i < nstokes; i++) {
                        ranks.tovector(cubeStats[i].percentileRanks);
                        cubeStats[i].percentiles.resize(numRanks);
                        for (auto j = 0; j < numRanks; j++) {
                            cubeStats[i].percentiles[j] = vals(i,j);
                        }
                    }                    
                }
            }
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
