#include "Frame.h"
#include <cmath>
#include <fmt/format.h>
#include <H5Cpp.h>
#include <H5File.h>
#include "util.h"

using namespace std;
using namespace H5;
Frame::Frame(const string& uuidString, const string& filename, const string& hdu, int defaultChannel)
    :uuid(uuidString),
     valid(true)
{
    try {
        file = H5File(filename, H5F_ACC_RDONLY);

        imageInfo.filename = filename;
        hduGroup = file.openGroup(hdu);

        DataSet dataSet = hduGroup.openDataSet("DATA");
        vector<hsize_t> dims(dataSet.getSpace().getSimpleExtentNdims(), 0);
        dataSet.getSpace().getSimpleExtentDims(dims.data(), NULL);

        imageInfo.dimensions = dims.size();

        if (imageInfo.dimensions < 2 || imageInfo.dimensions > 4) {
            log(uuid, "Problem loading file {}: Image must be 2D, 3D or 4D.", filename);
            valid = false;
            return;
        }

        imageInfo.width = dims[imageInfo.dimensions - 1];
        imageInfo.height = dims[imageInfo.dimensions - 2];
        imageInfo.depth = (imageInfo.dimensions > 2) ? dims[imageInfo.dimensions - 3] : 1;
        imageInfo.stokes = (imageInfo.dimensions > 3) ? dims[imageInfo.dimensions - 4] : 1;

        dataSets.clear();
        dataSets["main"] = dataSet;

        loadStats();

        // Swizzled data loaded if it exists. Used for Z-profiles and region stats
        if (H5Lexists(hduGroup.getId(), "SwizzledData", 0)) {
            if (imageInfo.dimensions == 3 && H5Lexists(hduGroup.getId(), "SwizzledData/ZYX", 0)) {
                DataSet dataSetSwizzled = hduGroup.openDataSet("SwizzledData/ZYX");
                vector<hsize_t> swizzledDims(dataSetSwizzled.getSpace().getSimpleExtentNdims(), 0);
                dataSetSwizzled.getSpace().getSimpleExtentDims(swizzledDims.data(), NULL);

                if (swizzledDims.size() != 3 || swizzledDims[0] != dims[2]) {
                    log(uuid, "Invalid swizzled data set in file {}, ignoring.", filename);
                } else {
                    log(uuid, "Found valid swizzled data set in file {}.", filename);
                    dataSets["swizzled"] = dataSetSwizzled;
                }
            } else if (imageInfo.dimensions == 4 && H5Lexists(hduGroup.getId(), "SwizzledData/ZYXW", 0)) {
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

bool Frame::setChannels(int channel, int stokes) {
    if (!valid) {
        log(uuid, "No file loaded");
        return false;
    } else if (channel < 0 || channel >= imageInfo.depth || stokes < 0 || stokes >= imageInfo.stokes) {
        log(uuid, "Channel {} (stokes {}) is invalid in file {}", channel, stokes, imageInfo.filename);
        return false;
    }

    // Define dimensions of hyperslab in 2D
    vector<hsize_t> count = {imageInfo.height, imageInfo.width};
    vector<hsize_t> start = {0, 0};

    // Append channel (and stokes in 4D) to hyperslab dims
    if (imageInfo.dimensions == 3) {
        count.insert(count.begin(), 1);
        start.insert(start.begin(), channel);
    } else if (imageInfo.dimensions == 4) {
        count.insert(count.begin(), {1, 1});
        start.insert(start.begin(), {stokes, channel});
    }

    // Read data into memory space
    hsize_t memDims[] = {imageInfo.height, imageInfo.width};
    DataSpace memspace(2, memDims);
    channelCache.resize(imageInfo.width * imageInfo.height);
    auto sliceDataSpace = dataSets["main"].getSpace();
    sliceDataSpace.selectHyperslab(H5S_SELECT_SET, count.data(), start.data());
    dataSets["main"].read(channelCache.data(), PredType::NATIVE_FLOAT, memspace, sliceDataSpace);

    currentStokes = stokes;
    currentChannel = channel;
    //updateHistogram();
    return true;
}

bool Frame::loadStats() {
    if (!valid) {
        log(uuid, "No file loaded");
        return false;
    }

    imageInfo.channelStats.resize(imageInfo.stokes);
    for (auto i = 0; i < imageInfo.stokes; i++) {
        imageInfo.channelStats[i].resize(imageInfo.depth);
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
            if (imageInfo.dimensions == 2 && dims.size() == 0) {
                dataSet.read(&imageInfo.channelStats[0][0].maxVal, PredType::NATIVE_FLOAT);
            } // 3D cubes
            else if (imageInfo.dimensions == 3 && dims.size() == 1 && dims[0] == imageInfo.depth) {
                vector<float> data(imageInfo.depth);
                dataSet.read(data.data(), PredType::NATIVE_FLOAT);
                for (auto i = 0; i < imageInfo.depth; i++) {
                    imageInfo.channelStats[0][i].maxVal = data[i];
                }
            } // 4D cubes
            else if (imageInfo.dimensions == 4 && dims.size() == 2 && dims[0] == imageInfo.stokes && dims[1] == imageInfo.depth) {
                vector<float> data(imageInfo.depth * imageInfo.stokes);
                dataSet.read(data.data(), PredType::NATIVE_FLOAT);
                for (auto i = 0; i < imageInfo.stokes; i++) {
                    for (auto j = 0; j < imageInfo.depth; j++) {
                        imageInfo.channelStats[i][j].maxVal = data[i * imageInfo.depth + j];
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
            if (imageInfo.dimensions == 2 && dims.size() == 0) {
                dataSet.read(&imageInfo.channelStats[0][0].minVal, PredType::NATIVE_FLOAT);
            } // 3D cubes
            else if (imageInfo.dimensions == 3 && dims.size() == 1 && dims[0] == imageInfo.depth) {
                vector<float> data(imageInfo.depth);
                dataSet.read(data.data(), PredType::NATIVE_FLOAT);
                for (auto i = 0; i < imageInfo.depth; i++) {
                    imageInfo.channelStats[0][i].minVal = data[i];
                }
            } // 4D cubes
            else if (imageInfo.dimensions == 4 && dims.size() == 2 && dims[0] == imageInfo.stokes && dims[1] == imageInfo.depth) {
                vector<float> data(imageInfo.stokes * imageInfo.depth);
                dataSet.read(data.data(), PredType::NATIVE_FLOAT);
                for (auto i = 0; i < imageInfo.stokes; i++) {
                    for (auto j = 0; j < imageInfo.depth; j++) {
                        imageInfo.channelStats[i][j].minVal = data[i * imageInfo.depth + j];
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
            if (imageInfo.dimensions == 2 && dims.size() == 0) {
                dataSet.read(&imageInfo.channelStats[0][0].mean, PredType::NATIVE_FLOAT);
            } // 3D cubes
            else if (imageInfo.dimensions == 3 && dims.size() == 1 && dims[0] == imageInfo.depth) {
                vector<float> data(imageInfo.depth);
                dataSet.read(data.data(), PredType::NATIVE_FLOAT);
                for (auto i = 0; i < imageInfo.depth; i++) {
                    imageInfo.channelStats[0][i].mean = data[i];
                }
            } // 4D cubes
            else if (imageInfo.dimensions == 4 && dims.size() == 2 && dims[0] == imageInfo.stokes && dims[1] == imageInfo.depth) {
                vector<float> data(imageInfo.stokes * imageInfo.depth);
                dataSet.read(data.data(), PredType::NATIVE_FLOAT);
                for (auto i = 0; i < imageInfo.stokes; i++) {
                    for (auto j = 0; j < imageInfo.depth; j++) {
                        imageInfo.channelStats[i][j].mean = data[i * imageInfo.depth + j];
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
            if (imageInfo.dimensions == 2 && dims.size() == 0) {
                dataSet.read(&imageInfo.channelStats[0][0].nanCount, PredType::NATIVE_INT64);
            } // 3D cubes
            else if (imageInfo.dimensions == 3 && dims.size() == 1 && dims[0] == imageInfo.depth) {
                vector<int64_t> data(imageInfo.depth);
                dataSet.read(data.data(), PredType::NATIVE_INT64);
                for (auto i = 0; i < imageInfo.depth; i++) {
                    imageInfo.channelStats[0][i].nanCount = data[i];
                }
            } // 4D cubes
            else if (imageInfo.dimensions == 4 && dims.size() == 2 && dims[0] == imageInfo.stokes && dims[1] == imageInfo.depth) {
                vector<int64_t> data(imageInfo.stokes * imageInfo.depth);
                dataSet.read(data.data(), PredType::NATIVE_INT64);
                for (auto i = 0; i < imageInfo.stokes; i++) {
                    for (auto j = 0; j < imageInfo.depth; j++) {
                        imageInfo.channelStats[i][j].nanCount = data[i * imageInfo.depth + j];
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
            if (imageInfo.dimensions == 2) {
                auto numBins = dims[0];
                vector<int> data(numBins);
                dataSet.read(data.data(), PredType::NATIVE_INT);
                imageInfo.channelStats[0][0].histogramBins = data;
            } // 3D cubes
            else if (imageInfo.dimensions == 3 && dims.size() == 2 && dims[0] == imageInfo.depth) {
                auto numBins = dims[1];
                vector<int> data(imageInfo.depth * numBins);
                dataSet.read(data.data(), PredType::NATIVE_INT);
                for (auto i = 0; i < imageInfo.depth; i++) {
                    imageInfo.channelStats[0][i].histogramBins.resize(numBins);
                    for (auto j = 0; j < numBins; j++) {
                        imageInfo.channelStats[0][i].histogramBins[j] = data[i * numBins + j];
                    }
                }
            } // 4D cubes
            else if (imageInfo.dimensions == 4 && dims.size() == 3 && dims[0] == imageInfo.stokes && dims[1] == imageInfo.depth) {
                auto numBins = dims[2];
                vector<int> data(imageInfo.stokes * imageInfo.depth * numBins);
                dataSet.read(data.data(), PredType::NATIVE_INT);
                for (auto i = 0; i < imageInfo.stokes; i++) {
                    for (auto j = 0; j < imageInfo.depth; j++) {
                        auto& stats = imageInfo.channelStats[i][j];
                        stats.histogramBins.resize(numBins);
                        for (auto k = 0; k < numBins; k++) {
                            stats.histogramBins[k] = data[(i * imageInfo.depth + j) * numBins + k];
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

            if (imageInfo.dimensions == 2 && dims.size() == 1 && dims[0] == numRanks) {
                vector<float> vals(numRanks);
                dataSetPercentiles.read(vals.data(), PredType::NATIVE_FLOAT);
                imageInfo.channelStats[0][0].percentiles = vals;
                imageInfo.channelStats[0][0].percentileRanks = ranks;
            }
                // 3D cubes
            else if (imageInfo.dimensions == 3 && dims.size() == 2 && dims[0] == imageInfo.depth && dims[1] == numRanks) {
                vector<float> vals(imageInfo.depth * numRanks);
                dataSetPercentiles.read(vals.data(), PredType::NATIVE_FLOAT);

                for (auto i = 0; i < imageInfo.depth; i++) {
                    imageInfo.channelStats[0][i].percentileRanks = ranks;
                    imageInfo.channelStats[0][i].percentiles.resize(numRanks);
                    for (auto j = 0; j < numRanks; j++) {
                        imageInfo.channelStats[0][i].percentiles[j] = vals[i * numRanks + j];
                    }
                }
            }
                // 4D cubes
            else if (imageInfo.dimensions == 4 && dims.size() == 3 && dims[0] == imageInfo.stokes && dims[1] == imageInfo.depth && dims[2] == numRanks) {
                vector<float> vals(imageInfo.stokes * imageInfo.depth * numRanks);
                dataSetPercentiles.read(vals.data(), PredType::NATIVE_FLOAT);

                for (auto i = 0; i < imageInfo.stokes; i++) {
                    for (auto j = 0; j < imageInfo.depth; j++) {
                        auto& stats = imageInfo.channelStats[i][j];
                        stats.percentiles.resize(numRanks);
                        for (auto k = 0; k < numRanks; k++) {
                            stats.percentiles[k] = vals[(i * imageInfo.depth + j) * numRanks + k];
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

vector<float> Frame::getImageData(CARTA::ImageBounds imageBounds, int mip, bool meanFilter) {
    if (!valid) {
        return vector<float>();
    }

    const int x = imageBounds.x_min();
    const int y = imageBounds.y_min();
    const int height = imageBounds.y_max() - imageBounds.y_min();
    const int width = imageBounds.x_max() - imageBounds.x_min();

    if (imageInfo.height < y + height || imageInfo.width < x + width) {
        return vector<float>();
    }

    size_t numRowsRegion = height / mip;
    size_t rowLengthRegion = width / mip;
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
                        float pixVal = channelCache[(y + j * mip + pixelY) * imageInfo.width + (x + i * mip + pixelX)];
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
                regionData[j * rowLengthRegion + i] = channelCache[(y + j * mip) * imageInfo.width + (x + i * mip)];
            }
        }
    }
    return regionData;
}
