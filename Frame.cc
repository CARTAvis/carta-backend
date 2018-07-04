#include "Frame.h"
#include <cmath>
#include "util.h"

using namespace std;
using namespace H5;

Frame::Frame(const string& uuidString, const string& filename, const string& hdu, int defaultChannel)
    : uuid(uuidString),
      valid(true),
      filename(filename) {
    try {
        file = H5File(filename, H5F_ACC_RDONLY);
        hduGroup = file.openGroup(hdu);
        DataSet dataSet = hduGroup.openDataSet("DATA");
        vector<hsize_t> dims(dataSet.getSpace().getSimpleExtentNdims(), 0);
        dataSet.getSpace().getSimpleExtentDims(dims.data(), NULL);
        dimensions = dims.size();

        if (dimensions < 2 || dimensions > 4) {
            log(uuid, "Problem loading file {}: Image must be 2D, 3D or 4D.", filename);
            valid = false;
            return;
        }

        width = dims[dimensions - 1];
        height = dims[dimensions - 2];
        depth = (dimensions > 2) ? dims[dimensions - 3] : 1;
        stokes = (dimensions > 3) ? dims[dimensions - 4] : 1;

        dataSets.clear();
        dataSets["main"] = dataSet;

        loadStats();

        // Swizzled data loaded if it exists. Used for Z-profiles and region stats
        if (H5Lexists(hduGroup.getId(), "SwizzledData", 0)) {
            if (dimensions == 3 && H5Lexists(hduGroup.getId(), "SwizzledData/ZYX", 0)) {
                DataSet dataSetSwizzled = hduGroup.openDataSet("SwizzledData/ZYX");
                vector<hsize_t> swizzledDims(dataSetSwizzled.getSpace().getSimpleExtentNdims(), 0);
                dataSetSwizzled.getSpace().getSimpleExtentDims(swizzledDims.data(), NULL);

                if (swizzledDims.size() != 3 || swizzledDims[0] != dims[2]) {
                    log(uuid, "Invalid swizzled data set in file {}, ignoring.", filename);
                } else {
                    log(uuid, "Found valid swizzled data set in file {}.", filename);
                    dataSets["swizzled"] = dataSetSwizzled;
                }
            } else if (dimensions == 4 && H5Lexists(hduGroup.getId(), "SwizzledData/ZYXW", 0)) {
                DataSet dataSetSwizzled = hduGroup.openDataSet("SwizzledData/ZYXW");
                vector<hsize_t> swizzledDims(dataSetSwizzled.getSpace().getSimpleExtentNdims(), 0);
                dataSetSwizzled.getSpace().getSimpleExtentDims(swizzledDims.data(), NULL);
                if (swizzledDims.size() != 4 || swizzledDims[1] != dims[3]) {
                    log(uuid, "Invalid swizzled data set in file {}, ignoring.", filename);
                } else {
                    log(uuid, "Found valid swizzled data set in file {}.", filename);
                    dataSets["swizzled"] = dataSetSwizzled;
                }
            } else {
                log(uuid, "File {} missing optional swizzled data set, using fallback calculation.", filename);
            }
        } else {
            log(uuid, "File {} missing optional swizzled data set, using fallback calculation.", filename);
        }
        valid = setChannels(defaultChannel, 0);
    }
    catch (FileIException& err) {
        log(uuid, "Problem loading file {}", filename);
        valid = false;
    }
}

bool Frame::isValid() {
    return valid;
}

bool Frame::setChannels(int newChannel, int newStokes) {
    if (!valid) {
        log(uuid, "No file loaded");
        return false;
    } else if (newChannel < 0 || newChannel >= depth || newStokes < 0 || newStokes >= stokes) {
        log(uuid, "Channel {} (stokes {}) is invalid in file {}", newChannel, newStokes, filename);
        return false;
    }

    // Define dimensions of hyperslab in 2D
    vector<hsize_t> count = {height, width};
    vector<hsize_t> start = {0, 0};

    // Append channel (and stokes in 4D) to hyperslab dims
    if (dimensions == 3) {
        count.insert(count.begin(), 1);
        start.insert(start.begin(), newChannel);
    } else if (dimensions == 4) {
        count.insert(count.begin(), {1, 1});
        start.insert(start.begin(), {newStokes, newChannel});
    }

    // Read data into memory space
    hsize_t memDims[] = {height, width};
    DataSpace memspace(2, memDims);
    channelCache.resize(width * height);
    auto sliceDataSpace = dataSets["main"].getSpace();
    sliceDataSpace.selectHyperslab(H5S_SELECT_SET, count.data(), start.data());
    dataSets["main"].read(channelCache.data(), PredType::NATIVE_FLOAT, memspace, sliceDataSpace);

    stokesIndex = newStokes;
    channelIndex = newChannel;
    //updateHistogram();
    return true;
}

bool Frame::loadStats() {
    if (!valid) {
        log(uuid, "No file loaded");
        return false;
    }

    channelStats.resize(stokes);
    for (auto i = 0; i < stokes; i++) {
        channelStats[i].resize(depth);
    }

    //TODO: Support multiple HDUs
    if (H5Lexists(hduGroup.getId(), "Statistics", 0) && H5Lexists(hduGroup.getId(), "Statistics/XY", 0)) {
        auto statsGroup = hduGroup.openGroup("Statistics/XY");
        if (H5Lexists(statsGroup.getId(), "MAX", 0)) {
            auto dataSet = statsGroup.openDataSet("MAX");
            auto dataSpace = dataSet.getSpace();
            vector<hsize_t> dims(dataSpace.getSimpleExtentNdims(), 0);
            dataSpace.getSimpleExtentDims(dims.data(), NULL);

            // 2D cubes
            if (dimensions == 2 && dims.size() == 0) {
                dataSet.read(&channelStats[0][0].maxVal, PredType::NATIVE_FLOAT);
            } // 3D cubes
            else if (dimensions == 3 && dims.size() == 1 && dims[0] == depth) {
                vector<float> data(depth);
                dataSet.read(data.data(), PredType::NATIVE_FLOAT);
                for (auto i = 0; i < depth; i++) {
                    channelStats[0][i].maxVal = data[i];
                }
            } // 4D cubes
            else if (dimensions == 4 && dims.size() == 2 && dims[0] == stokes && dims[1] == depth) {
                vector<float> data(depth * stokes);
                dataSet.read(data.data(), PredType::NATIVE_FLOAT);
                for (auto i = 0; i < stokes; i++) {
                    for (auto j = 0; j < depth; j++) {
                        channelStats[i][j].maxVal = data[i * depth + j];
                    }
                }
            } else {
                log(uuid, "Invalid MaxVals statistics");
                return false;
            }

        } else {
            log(uuid, "Missing MaxVals statistics");
            return false;
        }

        if (H5Lexists(statsGroup.getId(), "MIN", 0)) {
            auto dataSet = statsGroup.openDataSet("MIN");
            auto dataSpace = dataSet.getSpace();
            vector<hsize_t> dims(dataSpace.getSimpleExtentNdims(), 0);
            dataSpace.getSimpleExtentDims(dims.data(), NULL);

            // 2D cubes
            if (dimensions == 2 && dims.size() == 0) {
                dataSet.read(&channelStats[0][0].minVal, PredType::NATIVE_FLOAT);
            } // 3D cubes
            else if (dimensions == 3 && dims.size() == 1 && dims[0] == depth) {
                vector<float> data(depth);
                dataSet.read(data.data(), PredType::NATIVE_FLOAT);
                for (auto i = 0; i < depth; i++) {
                    channelStats[0][i].minVal = data[i];
                }
            } // 4D cubes
            else if (dimensions == 4 && dims.size() == 2 && dims[0] == stokes && dims[1] == depth) {
                vector<float> data(stokes * depth);
                dataSet.read(data.data(), PredType::NATIVE_FLOAT);
                for (auto i = 0; i < stokes; i++) {
                    for (auto j = 0; j < depth; j++) {
                        channelStats[i][j].minVal = data[i * depth + j];
                    }
                }
            } else {
                log(uuid, "Invalid MinVals statistics");
                return false;
            }

        } else {
            log(uuid, "Missing MinVals statistics");
            return false;
        }

        if (H5Lexists(statsGroup.getId(), "MEAN", 0)) {
            auto dataSet = statsGroup.openDataSet("MEAN");
            auto dataSpace = dataSet.getSpace();
            vector<hsize_t> dims(dataSpace.getSimpleExtentNdims(), 0);
            dataSpace.getSimpleExtentDims(dims.data(), NULL);

            // 2D cubes
            if (dimensions == 2 && dims.size() == 0) {
                dataSet.read(&channelStats[0][0].mean, PredType::NATIVE_FLOAT);
            } // 3D cubes
            else if (dimensions == 3 && dims.size() == 1 && dims[0] == depth) {
                vector<float> data(depth);
                dataSet.read(data.data(), PredType::NATIVE_FLOAT);
                for (auto i = 0; i < depth; i++) {
                    channelStats[0][i].mean = data[i];
                }
            } // 4D cubes
            else if (dimensions == 4 && dims.size() == 2 && dims[0] == stokes && dims[1] == depth) {
                vector<float> data(stokes * depth);
                dataSet.read(data.data(), PredType::NATIVE_FLOAT);
                for (auto i = 0; i < stokes; i++) {
                    for (auto j = 0; j < depth; j++) {
                        channelStats[i][j].mean = data[i * depth + j];
                    }
                }
            } else {
                log(uuid, "Invalid Means statistics");
                return false;
            }
        } else {
            log(uuid, "Missing Means statistics");
            return false;
        }

        if (H5Lexists(statsGroup.getId(), "NAN_COUNT", 0)) {
            auto dataSet = statsGroup.openDataSet("NAN_COUNT");
            auto dataSpace = dataSet.getSpace();
            vector<hsize_t> dims(dataSpace.getSimpleExtentNdims(), 0);
            dataSpace.getSimpleExtentDims(dims.data(), NULL);

            // 2D cubes
            if (dimensions == 2 && dims.size() == 0) {
                dataSet.read(&channelStats[0][0].nanCount, PredType::NATIVE_INT64);
            } // 3D cubes
            else if (dimensions == 3 && dims.size() == 1 && dims[0] == depth) {
                vector<int64_t> data(depth);
                dataSet.read(data.data(), PredType::NATIVE_INT64);
                for (auto i = 0; i < depth; i++) {
                    channelStats[0][i].nanCount = data[i];
                }
            } // 4D cubes
            else if (dimensions == 4 && dims.size() == 2 && dims[0] == stokes && dims[1] == depth) {
                vector<int64_t> data(stokes * depth);
                dataSet.read(data.data(), PredType::NATIVE_INT64);
                for (auto i = 0; i < stokes; i++) {
                    for (auto j = 0; j < depth; j++) {
                        channelStats[i][j].nanCount = data[i * depth + j];
                    }
                }
            } else {
                log(uuid, "Invalid NaNCounts statistics");
                return false;
            }
        } else {
            log(uuid, "Missing NaNCounts statistics");
            return false;
        }

        if (H5Lexists(statsGroup.getId(), "HISTOGRAM", 0)) {
            auto dataSet = statsGroup.openDataSet("HISTOGRAM");
            auto dataSpace = dataSet.getSpace();
            vector<hsize_t> dims(dataSpace.getSimpleExtentNdims(), 0);
            dataSpace.getSimpleExtentDims(dims.data(), NULL);

            // 2D cubes
            if (dimensions == 2) {
                auto numBins = dims[0];
                vector<int> data(numBins);
                dataSet.read(data.data(), PredType::NATIVE_INT);
                channelStats[0][0].histogramBins = data;
            } // 3D cubes
            else if (dimensions == 3 && dims.size() == 2 && dims[0] == depth) {
                auto numBins = dims[1];
                vector<int> data(depth * numBins);
                dataSet.read(data.data(), PredType::NATIVE_INT);
                for (auto i = 0; i < depth; i++) {
                    channelStats[0][i].histogramBins.resize(numBins);
                    for (auto j = 0; j < numBins; j++) {
                        channelStats[0][i].histogramBins[j] = data[i * numBins + j];
                    }
                }
            } // 4D cubes
            else if (dimensions == 4 && dims.size() == 3 && dims[0] == stokes && dims[1] == depth) {
                auto numBins = dims[2];
                vector<int> data(stokes * depth * numBins);
                dataSet.read(data.data(), PredType::NATIVE_INT);
                for (auto i = 0; i < stokes; i++) {
                    for (auto j = 0; j < depth; j++) {
                        auto& stats = channelStats[i][j];
                        stats.histogramBins.resize(numBins);
                        for (auto k = 0; k < numBins; k++) {
                            stats.histogramBins[k] = data[(i * depth + j) * numBins + k];
                        }
                    }
                }
            } else {
                log(uuid, "Invalid histogram statistics");
                return false;
            }

        } else {
            log(uuid, "Missing Histograms group");
            return false;
        }
        if (H5Lexists(statsGroup.getId(), "PERCENTILES", 0) && H5Lexists(hduGroup.getId(), "PERCENTILE_RANKS", 0)) {
            auto dataSetPercentiles = statsGroup.openDataSet("PERCENTILES");
            auto dataSetPercentilesRank = hduGroup.openDataSet("PERCENTILE_RANKS");

            auto dataSpacePercentiles = dataSetPercentiles.getSpace();
            vector<hsize_t> dims(dataSpacePercentiles.getSimpleExtentNdims(), 0);
            dataSpacePercentiles.getSimpleExtentDims(dims.data(), NULL);
            auto dataSpaceRank = dataSetPercentilesRank.getSpace();
            vector<hsize_t> dimsRanks(dataSpaceRank.getSimpleExtentNdims(), 0);
            dataSpaceRank.getSimpleExtentDims(dimsRanks.data(), NULL);

            auto numRanks = dimsRanks[0];
            vector<float> ranks(numRanks);
            dataSetPercentilesRank.read(ranks.data(), PredType::NATIVE_FLOAT);

            if (dimensions == 2 && dims.size() == 1 && dims[0] == numRanks) {
                vector<float> vals(numRanks);
                dataSetPercentiles.read(vals.data(), PredType::NATIVE_FLOAT);
                channelStats[0][0].percentiles = vals;
                channelStats[0][0].percentileRanks = ranks;
            }
                // 3D cubes
            else if (dimensions == 3 && dims.size() == 2 && dims[0] == depth && dims[1] == numRanks) {
                vector<float> vals(depth * numRanks);
                dataSetPercentiles.read(vals.data(), PredType::NATIVE_FLOAT);

                for (auto i = 0; i < depth; i++) {
                    channelStats[0][i].percentileRanks = ranks;
                    channelStats[0][i].percentiles.resize(numRanks);
                    for (auto j = 0; j < numRanks; j++) {
                        channelStats[0][i].percentiles[j] = vals[i * numRanks + j];
                    }
                }
            }
                // 4D cubes
            else if (dimensions == 4 && dims.size() == 3 && dims[0] == stokes && dims[1] == depth && dims[2] == numRanks) {
                vector<float> vals(stokes * depth * numRanks);
                dataSetPercentiles.read(vals.data(), PredType::NATIVE_FLOAT);

                for (auto i = 0; i < stokes; i++) {
                    for (auto j = 0; j < depth; j++) {
                        auto& stats = channelStats[i][j];
                        stats.percentiles.resize(numRanks);
                        for (auto k = 0; k < numRanks; k++) {
                            stats.percentiles[k] = vals[(i * depth + j) * numRanks + k];
                        }
                        stats.percentileRanks = ranks;
                    }
                }
            } else {
                log(uuid, "Missing Percentiles datasets");
                return false;
            }
        } else {
            log(uuid, "Missing Percentiles group");
            return false;
        }

    } else {
        log(uuid, "Missing Statistics group");
        return false;
    }

    return true;
}

int Frame::currentStokes() {
    return stokesIndex;
}

int Frame::currentChannel() {
    return channelIndex;
}

vector<float> Frame::getImageData(CARTA::ImageBounds imageBounds, int mip, bool meanFilter) {
    if (!valid) {
        return vector<float>();
    }

    const int x = imageBounds.x_min();
    const int y = imageBounds.y_min();
    const int reqHeight = imageBounds.y_max() - imageBounds.y_min();
    const int reqWidth = imageBounds.x_max() - imageBounds.x_min();

    if (height < y + reqHeight || width < x + reqWidth) {
        return vector<float>();
    }

    size_t numRowsRegion = reqHeight / mip;
    size_t rowLengthRegion = reqWidth / mip;
    vector<float> regionData;
    regionData.resize(numRowsRegion * rowLengthRegion);

    if (meanFilter) {
        // Perform down-sampling by calculating the mean for each MIPxMIP block
        for (auto j = 0; j < numRowsRegion; j++) {
            for (auto i = 0; i < rowLengthRegion; i++) {
                float pixelSum = 0;
                int pixelCount = 0;
                for (auto pixelX = 0; pixelX < mip; pixelX++) {
                    for (auto pixelY = 0; pixelY < mip; pixelY++) {
                        float pixVal = channelCache[(y + j * mip + pixelY) * reqWidth + (x + i * mip + pixelX)];
                        if (!isnan(pixVal)) {
                            pixelCount++;
                            pixelSum += pixVal;
                        }
                    }
                }
                regionData[j * rowLengthRegion + i] = pixelCount ? pixelSum / pixelCount : NAN;
            }
        }
    } else {
        // Nearest neighbour filtering
        for (auto j = 0; j < numRowsRegion; j++) {
            for (auto i = 0; i < rowLengthRegion; i++) {
                regionData[j * rowLengthRegion + i] = channelCache[(y + j * mip) * reqWidth + (x + i * mip)];
            }
        }
    }
    return regionData;
}
