#include "Frame.h"
#include "util.h"

#include <memory>
#include <tbb/tbb.h>

using namespace carta;
using namespace std;

Frame::Frame(const string& uuidString, const string& filename, const string& hdu, int defaultChannel)
    : uuid(uuidString),
      valid(true),
      filename(filename),
      loader(FileLoader::getLoader(filename)),
      spectralAxis(-1), stokesAxis(-1) {
    try {
        if (loader==nullptr) {
            log(uuid, "Problem loading file {}: loader not implemented", filename);
            valid = false;
            return;
        }
        loader->openFile(filename, hdu);
        auto &dataSet = loader->loadData(FileInfo::Data::XYZW);

        imageShape = dataSet.shape();
        ndims = imageShape.size();
        if (ndims < 2 || ndims > 4) {
            log(uuid, "Problem loading file {}: Image must be 2D, 3D or 4D.", filename);
            valid = false;
            return;
        }

        // determine axis order (0-based)
        if (ndims == 3) { // use defaults
            spectralAxis = 2;
            stokesAxis = -1;
        } else if (ndims == 4) {  // find spectral and stokes axes
            loader->findCoords(spectralAxis, stokesAxis);
        } 

        log(uuid, "Opening image with dimensions: {}", imageShape);
        // set current channel, stokes, channelCache
        channelCache.resize();
        std::string errMessage;
        valid = setImageChannels(defaultChannel, 0, errMessage);

        // make Region for entire image (after current channel/stokes set)
        setImageRegion();
        loadImageChannelStats(false); // from image file if exists

        // Swizzled data loaded if it exists. Used for Z-profiles and region stats
        if (ndims == 3 && loader->hasData(FileInfo::Data::ZYX)) {
            auto &dataSetSwizzled = loader->loadData(FileInfo::Data::ZYX);
            casacore::IPosition swizzledDims = dataSetSwizzled.shape();
            if (swizzledDims.size() != 3 || swizzledDims[0] != imageShape(2)) {
                log(uuid, "Invalid swizzled data set in file {}, ignoring.", filename);
            } else {
                log(uuid, "Found valid swizzled data set in file {}.", filename);
            }
        } else if (ndims == 4 && loader->hasData(FileInfo::Data::ZYXW)) {
            auto &dataSetSwizzled = loader->loadData(FileInfo::Data::ZYXW);
            casacore::IPosition swizzledDims = dataSetSwizzled.shape();
            if (swizzledDims.size() != 4 || swizzledDims[1] != imageShape(3)) {
                log(uuid, "Invalid swizzled data set in file {}, ignoring.", filename);
            } else {
                log(uuid, "Found valid swizzled data set in file {}.", filename);
            }
        } else {
            log(uuid, "File {} missing optional swizzled data set, using fallback calculation.", filename);
        }
    }
    //TBD: figure out what exceptions need to caught, if any
    catch (casacore::AipsError& err) {
        log(uuid, "Problem loading file {}", filename);
        log(uuid, err.getMesg());
        valid = false;
    }
}

Frame::~Frame() {
    for (auto& region : regions) {
        region.second.reset();
    }
    regions.clear();
}

bool Frame::isValid() {
    return valid;
}

int Frame::getMaxRegionId() {
    int maxRegionId(INT_MIN);
    for (auto it = regions.begin(); it != regions.end(); ++it)
        maxRegionId = max(maxRegionId, it->first);
    return maxRegionId;
}

// ********************************************************************
// Image data

std::vector<float> Frame::getImageData(bool meanFilter) {
    if (!valid) {
        return std::vector<float>();
    }

    CARTA::ImageBounds bounds(currentBounds());
    const int x = bounds.x_min();
    const int y = bounds.y_min();
    const int reqHeight = bounds.y_max() - bounds.y_min();
    const int reqWidth = bounds.x_max() - bounds.x_min();

    if (imageShape(1) < y + reqHeight || imageShape(0) < x + reqWidth) {
        return std::vector<float>();
    }

    size_t numRowsRegion = reqHeight / mip;
    size_t rowLengthRegion = reqWidth / mip;
    vector<float> regionData;
    regionData.resize(numRowsRegion * rowLengthRegion);

    if (meanFilter) {
        // Perform down-sampling by calculating the mean for each MIPxMIP block
        auto range = tbb::blocked_range2d<size_t>(0, numRowsRegion, 0, rowLengthRegion);
        auto loop = [&](const tbb::blocked_range2d<size_t> &r) {
            for(size_t j = r.rows().begin(); j != r.rows().end(); ++j) {
                for(size_t i = r.cols().begin(); i != r.cols().end(); ++i) {
                    float pixelSum = 0;
                    int pixelCount = 0;
                    for (auto pixelX = 0; pixelX < mip; pixelX++) {
                        for (auto pixelY = 0; pixelY < mip; pixelY++) {
                            auto imageRow = y + j * mip + pixelY;
                            auto imageCol = x + i * mip + pixelX;
                            float pixVal = channelCache(imageCol, imageRow);
                            if (!isnan(pixVal) && !isinf(pixVal)) {
                                pixelCount++;
                                pixelSum += pixVal;
                            }
                        }
                    }
                    regionData[j * rowLengthRegion + i] = pixelCount ? pixelSum / pixelCount : NAN;
                }
            }
        };
        tbb::parallel_for(range, loop);
    } else {
        // Nearest neighbour filtering
        auto range = tbb::blocked_range2d<size_t>(0, numRowsRegion, 0, rowLengthRegion);
        auto loop = [&](const tbb::blocked_range2d<size_t> &r) {
            for (auto j = 0; j < numRowsRegion; j++) {
                for (auto i = 0; i < rowLengthRegion; i++) {
                    auto imageRow = y + j * mip;
                    auto imageCol = x + i * mip;
                    regionData[j * rowLengthRegion + i] = channelCache(imageCol, imageRow);
                }
            }
        };
        tbb::parallel_for(range, loop);
    }
    return regionData;
}

bool Frame::loadImageChannelStats(bool loadPercentiles) {
    // load channel stats for entire image (all channels and stokes) from header
    // channelStats[stokes][chan]
    if (!valid) {
        log(uuid, "No file loaded");
        return false;
    }

    size_t depth(spectralAxis>=0 ? imageShape(spectralAxis) : 1);
    size_t nstokes(stokesAxis>=0 ? imageShape(stokesAxis) : 1);
    channelStats.resize(nstokes);
    for (auto i = 0; i < nstokes; i++) {
        channelStats[i].resize(depth);
    }

    //TODO: Support multiple HDUs
    if (loader->hasData(FileInfo::Data::Stats) && loader->hasData(FileInfo::Data::Stats2D)) {
        if (loader->hasData(FileInfo::Data::S2DMax)) {
            auto &dataSet = loader->loadData(FileInfo::Data::S2DMax);
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
            } else {
                log(uuid, "Invalid MaxVals statistics");
                return false;
            }

        } else {
            log(uuid, "Missing MaxVals statistics");
            return false;
        }

        if (loader->hasData(FileInfo::Data::S2DMin)) {
            auto &dataSet = loader->loadData(FileInfo::Data::S2DMin);
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
            } else {
                log(uuid, "Invalid MinVals statistics");
                return false;
            }

        } else {
            log(uuid, "Missing MinVals statistics");
            return false;
        }

        if (loader->hasData(FileInfo::Data::S2DMean)) {
            auto &dataSet = loader->loadData(FileInfo::Data::S2DMean);
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
            } else {
                log(uuid, "Invalid Means statistics");
                return false;
            }
        } else {
            log(uuid, "Missing Means statistics");
            return false;
        }

        if (loader->hasData(FileInfo::Data::S2DNans)) {
            auto &dataSet = loader->loadData(FileInfo::Data::S2DNans);
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
            } else {
                log(uuid, "Invalid NaNCounts statistics");
                return false;
            }
        } else {
            log(uuid, "Missing NaNCounts statistics");
            return false;
        }

        if (loader->hasData(FileInfo::Data::S2DHist)) {
            auto &dataSet = loader->loadData(FileInfo::Data::S2DHist);
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
            } else {
                log(uuid, "Invalid histogram statistics");
                return false;
            }

        } else {
            log(uuid, "Missing Histograms group");
            return false;
        }

        if (loadPercentiles) {
            if (loader->hasData(FileInfo::Data::S2DPercent) &&
                loader->hasData(FileInfo::Data::Ranks)) {
                auto &dataSetPercentiles = loader->loadData(FileInfo::Data::S2DPercent);
                auto &dataSetPercentilesRank = loader->loadData(FileInfo::Data::Ranks);

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
                } else {
                    log(uuid, "Missing Percentiles datasets");
                    return false;
                }
            } else {
                log(uuid, "Missing Percentiles group");
                return false;
            }
        }
    } else {
        log(uuid, "Missing Statistics group");
        return false;
    }
    return true;
}

// ********************************************************************
// Image view

bool Frame::setBounds(CARTA::ImageBounds imageBounds, int newMip) {
    if (!valid) {
        return false;
    }

    const int x = imageBounds.x_min();
    const int y = imageBounds.y_min();
    const int reqHeight = imageBounds.y_max() - imageBounds.y_min();
    const int reqWidth = imageBounds.x_max() - imageBounds.x_min();

    if ((imageShape(1) < y + reqHeight) || (imageShape(0) < x + reqWidth)) {
        return false;
    }

    bounds = imageBounds;
    mip = newMip;
    return true;
}

CARTA::ImageBounds Frame::currentBounds() {
    return bounds;
}

int Frame::currentMip() {
    return mip;
}

// ********************************************************************
// Image channels

bool Frame::setImageChannels(int newChannel, int newStokes, std::string& message) {
    if (!valid) {
        message = "No file loaded";
        log(uuid, message);
        return false;
    } else {
        size_t depth(spectralAxis>=0 ? imageShape(spectralAxis) : 1);
        if (newChannel < 0 || newChannel >= depth) {
            message = fmt::format("Channel {} is invalid in file {}", newChannel, filename);
            log(uuid, message);
            return false;
        }
        size_t nstokes(stokesAxis>=0 ? imageShape(stokesAxis) : 1);
        if (newStokes < 0 || newStokes >= nstokes) {
            message = fmt::format("Stokes {} is invalid in file {}", newStokes, filename);
            log(uuid, message);
            return false;
        }
    }

    bool channelChanged(newChannel != currentChannel()),
        stokesChanged(newStokes != currentStokes());
    // update channelCache with new chan and stokes
    getChannelMatrix(channelCache, newChannel, newStokes);
    stokesIndex = newStokes;
    channelIndex = newChannel;

    // update Histogram with current channel
    if (channelChanged) {
        std::vector<CARTA::SetHistogramRequirements_HistogramConfig> configs;
        setRegionHistogramRequirements(IMAGE_REGION_ID, configs);
    }

    // update Spatial requirements with current channel/stokes
    if (channelChanged || stokesChanged) {
        std::vector<std::string> spatialProfiles;
        setRegionSpatialRequirements(CURSOR_REGION_ID, spatialProfiles);
    }

    // update Spectral requirements with current stokes
    if (stokesChanged) {
        std::vector<CARTA::SetSpectralRequirements_SpectralConfig> spectralProfiles;
        setRegionSpectralRequirements(CURSOR_REGION_ID, spectralProfiles);
    }

    return true;
}

void Frame::getChannelMatrix(casacore::Matrix<float>& chanMatrix, size_t channel, size_t stokes) {
    // matrix for given channel and stokes
    if (!channelCache.empty() && channel==channelIndex && stokes==stokesIndex) {
        // already cached
        chanMatrix.reference(channelCache);
        return;
    }
    
    // slice image data
    casacore::Slicer section = getChannelMatrixSlicer(channel, stokes);
    casacore::Array<float> tmp;
    std::unique_lock<std::mutex> guard(mutex);
    loader->loadData(FileInfo::Data::XYZW).getSlice(tmp, section, true);
    chanMatrix.reference(tmp);
}

casacore::Slicer Frame::getChannelMatrixSlicer(size_t channel, size_t stokes) {
    // slicer for spectral and stokes axes to select channel, stokes
    casacore::IPosition count(imageShape);
    casacore::IPosition start(imageShape.size());
    start = 0;

    if (spectralAxis >= 0) {
        start(spectralAxis) = channel;
        count(spectralAxis) = 1;
    }
    if (stokesAxis >= 0) {
        start(stokesAxis) = stokes;
        count(stokesAxis) = 1;
    }
    // slicer for image data
    casacore::Slicer section(start, count);
    return section;
}

void Frame::getProfileSlicer(casacore::Slicer& latticeSlicer, int x, int y, int channel, int stokes) {
    // to slice image data along x, y, or spectral axis (indicated with -1)
    // Start with entire image:
    casacore::IPosition count(imageShape);
    casacore::IPosition start(imageShape.size());
    start = 0;

    if (x >= 0) {
        start(0) = x;
        count(0) = 1;
    }

    if (y >= 0) { 
        start(1) = y;
        count(1) = 1;
    }

    if ((channel >= 0) && (spectralAxis >=0)) {
        start(spectralAxis) = channel;
        count(spectralAxis) = 1;
    }

    if ((stokes >= 0) && (stokesAxis >=0)) {
        start(stokesAxis) = stokes;
        count(stokesAxis) = 1;
    }
 
    casacore::Slicer section(start, count);
    latticeSlicer = section;
}

int Frame::currentChannel() {
    return channelIndex;
}

int Frame::currentStokes() {
    return stokesIndex;
}

// ********************************************************************
// Region

bool Frame::setRegion(int regionId, std::string name, CARTA::RegionType type, int minchan,
        int maxchan, std::vector<int>& stokes, std::vector<CARTA::Point>& points,
        float rotation, std::string& message) {
    // Create or update Region
    // check channel bounds and stokes
    int nchan(spectralAxis>=0 ? imageShape(spectralAxis) : 1);
    if ((minchan < 0 || minchan >= nchan) || (maxchan < 0 || maxchan>=nchan)) {
        message = "min/max region channel bounds out of range";
        return false;
    }
    if (stokes.empty()) {
        stokes.push_back(currentStokes());
    } else {
        int nstokes(stokesAxis>=0 ? imageShape(stokesAxis) : 1);
        for (auto stokeVal : stokes) {
            if (stokeVal < 0 || stokeVal >= nstokes) {
                message = "region stokes value out of range";
                return false;
            }
        }
    }

    // check region control points
    for (auto point : points) {
        if ((point.x() < 0 || point.x() >= imageShape(0)) ||
            (point.y() < 0 || point.y() >= imageShape(1))) {
            message = "region control point value out of range";
            return false;
        }
    }

    // create or update Region
    if (regions.count(regionId)) { // update Region
        auto& region = regions[regionId];
        region->setChannels(minchan, maxchan, stokes);
        region->setControlPoints(points);
        region->setRotation(rotation);
    }  else { // map new Region to region id
        auto region = unique_ptr<carta::Region>(new carta::Region(name, type));
        region->setChannels(minchan, maxchan, stokes);
        region->setControlPoints(points);
        region->setRotation(rotation);
        regions[regionId] = move(region);
    }
    return true;
}

// special cases of setRegion for image and cursor
void Frame::setImageRegion() {
    // create a Region for the entire image, regionId = -1
    // set image region channels: all channels, all stokes
    int minchan(0);
    int maxchan = (spectralAxis>0 ? imageShape(spectralAxis)-1 : 0);
    std::vector<int> allStokes;
    if (stokesAxis > 0) {
        for (int i=0; i<imageShape(stokesAxis); ++i)
            allStokes.push_back(i);
    }
    // control points: rectangle from top left (0,height-1) to bottom right (width-1,0)
    std::vector<CARTA::Point> points(2);
    CARTA::Point point;
    point.set_x(0);
    point.set_y(imageShape(1)-1); // height
    points.push_back(point);
    point.set_x(imageShape(0)-1); // width
    point.set_y(0);
    points.push_back(point);
    // rotation
    float rotation(0.0);

    // create new region
    std::string message;
    setRegion(IMAGE_REGION_ID, "image", CARTA::RECTANGLE, minchan, maxchan, allStokes, points,
        rotation, message);

    // histogram requirements: use current channel, for now
    std::vector<CARTA::SetHistogramRequirements_HistogramConfig> configs;
    setRegionHistogramRequirements(IMAGE_REGION_ID, configs);
}

bool Frame::setCursorRegion(int regionId, const CARTA::Point& point) {
    // a cursor is a region with one control point
    bool cursorOk(true);
    std::vector<CARTA::Point> points;
    points.push_back(point);
    // use current channel and stokes
    int currChan(currentChannel());
    std::vector<int> currStokes;
    currStokes.push_back(currentStokes());

    if (regions.count(regionId)) { // update point region
        // validate point
        if ((point.x() < 0 || point.x() >= imageShape(0)) ||
            (point.y() < 0 || point.y() >= imageShape(1))) {
            cursorOk = false;
        } else {
            auto& region = regions[regionId];
            region->setControlPoints(points);
            region->setChannels(currChan, currChan, currStokes);
        }
    } else { // create new point region
        float rotation(0.0);
        std::string message;
        cursorOk = setRegion(regionId, "cursor", CARTA::POINT, currChan, currChan, currStokes,
            points, rotation, message);
        if (cursorOk) {
            // spatial requirements
            std::vector<std::string> spatialProfiles;
            setRegionSpatialRequirements(regionId, spatialProfiles);
            // spectral requirements
            std::vector<CARTA::SetSpectralRequirements_SpectralConfig> spectralProfiles;
            setRegionSpectralRequirements(regionId, spectralProfiles);
        }
    }
    return cursorOk;
}

void Frame::removeRegion(int regionId) {
    if (regions.count(regionId)) {
        regions[regionId].reset();
        regions.erase(regionId);
    }
}

// ****************************************************
// region profiles

// ***** region requirements *****

bool Frame::setRegionHistogramRequirements(int regionId,
        const std::vector<CARTA::SetHistogramRequirements_HistogramConfig>& histograms) {
    // set channel and num_bins for required histograms
    if (regions.count(regionId)) {
        auto& region = regions[regionId];
        if (histograms.empty()) {  // default to current channel, auto bin size
            std::vector<CARTA::SetHistogramRequirements_HistogramConfig> defaultConfigs;
            CARTA::SetHistogramRequirements_HistogramConfig config;
            config.set_channel(-1);
            config.set_num_bins(-1);
            defaultConfigs.push_back(config);
            return region->setHistogramRequirements(defaultConfigs);
        } else {
            return region->setHistogramRequirements(histograms);
        }
    } else {
        return false;
    }
}

bool Frame::setRegionSpatialRequirements(int regionId, const std::vector<std::string>& profiles) {
    // set requested spatial profiles e.g. ["Qx", "Uy"] or just ["x","y"] to use current stokes
    if (!regions.count(regionId) && (regionId == CURSOR_REGION_ID)) {
        // frontend sends spatial reqs for cursor before SET_CURSOR; set cursor region
        CARTA::Point centerPoint;
        centerPoint.set_x(imageShape(0)/2);
        centerPoint.set_y(imageShape(1)/2);
        setCursorRegion(CURSOR_REGION_ID, centerPoint);
    }
    int nstokes(stokesAxis>=0 ? imageShape(stokesAxis) : 1);
    if (regions.count(regionId)) {
        auto& region = regions[regionId];
        if (profiles.empty()) {  // default to ["x", "y"]
            std::vector<std::string> defaultProfiles;
            defaultProfiles.push_back("x");
            defaultProfiles.push_back("y");
            return region->setSpatialRequirements(defaultProfiles, nstokes, currentStokes());
        } else {
            return region->setSpatialRequirements(profiles, nstokes, currentStokes());
        }
    } else {
        return false;
    }
}

bool Frame::setRegionSpectralRequirements(int regionId,
        const std::vector<CARTA::SetSpectralRequirements_SpectralConfig>& profiles) {
    // set requested spectral profiles e.g. ["Qz", "Uz"] or just ["z"] to use current stokes
    if (!regions.count(regionId) && (regionId == CURSOR_REGION_ID)) {
        // in case frontend sends spectral reqs for cursor before SET_CURSOR; set cursor region
        CARTA::Point centerPoint;
        centerPoint.set_x(imageShape(0)/2);
        centerPoint.set_y(imageShape(1)/2);
        setCursorRegion(CURSOR_REGION_ID, centerPoint);
    }
    int nstokes(stokesAxis>=0 ? imageShape(stokesAxis) : 1);
    if (regions.count(regionId)) {
        auto& region = regions[regionId];
        if (profiles.empty()) {  // default to ["z"], no stats
            std::vector<CARTA::SetSpectralRequirements_SpectralConfig> defaultProfiles;
            CARTA::SetSpectralRequirements_SpectralConfig config;
            config.set_coordinate("z");
            config.add_stats_types(CARTA::StatsType::None);
            defaultProfiles.push_back(config);
            return region->setSpectralRequirements(defaultProfiles, nstokes, currentStokes());
        } else {
            return region->setSpectralRequirements(profiles, nstokes, currentStokes());
        }
    } else {
        return false;
    }
}

bool Frame::setRegionStatsRequirements(int regionId, const std::vector<int> statsTypes) {
    bool regionOK(true);
    if (regions.count(regionId)) {
        auto& region = regions[regionId];
        region->setStatsRequirements(statsTypes);
    } else {
        regionOK = false;
    }
    return regionOK;
}

// ***** region data *****

bool Frame::fillRegionHistogramData(int regionId, CARTA::RegionHistogramData* histogramData) {
    bool histogramOK(false);
    if (regions.count(regionId)) {
        auto& region = regions[regionId];
        int currStokes(currentStokes());
        histogramData->set_stokes(currStokes);
        int defaultNumBins = int(max(sqrt(imageShape(0) * imageShape(1)), 2.0));
        for (int i=0; i<region->numHistogramConfigs(); ++i) {
            CARTA::SetHistogramRequirements_HistogramConfig config = region->getHistogramConfig(i);
            int configChannel(config.channel()), configNumBins(config.num_bins());
            if (configChannel == -1) configChannel = currentChannel();
            // fill Histograms
            auto newHistogram = histogramData->add_histograms();
            newHistogram->set_channel(configChannel);
            bool haveHistogram(false);
            if (configChannel >= 0) {
                // use histogram from image file if correct channel, stokes, and numBins
                auto& currentStats = channelStats[currStokes][configChannel];
                if (!channelStats[currStokes][configChannel].histogramBins.empty()) {
                    int nbins(currentStats.histogramBins.size());
                    if ((configNumBins < 0) || (configNumBins == nbins)) {
                        newHistogram->set_num_bins(nbins);
                        newHistogram->set_bin_width((currentStats.maxVal - currentStats.minVal) / nbins);
                        newHistogram->set_first_bin_center(currentStats.minVal + (newHistogram->bin_width()/2.0));
                        *newHistogram->mutable_bins() = {currentStats.histogramBins.begin(), currentStats.histogramBins.end()};
                        haveHistogram = true;
                    }
                }
            }
            if (!haveHistogram) { 
                // get histogram from Region
                casacore::Slicer latticeSlicer;
                casacore::Array<float> histogramArray;
                if (configChannel == -2) { // all channels in region
                    getProfileSlicer(latticeSlicer, -1, -1, -1, currStokes);
                    std::unique_lock<std::mutex> guard(mutex);
                    loader->loadData(FileInfo::Data::XYZW).getSlice(histogramArray, latticeSlicer, true);
                } else { // requested channel (current or specified)
                    casacore::Matrix<float> chanMatrix;
                    getChannelMatrix(chanMatrix, configChannel, currStokes);
                    histogramArray.reference(chanMatrix);
                }
                if (configNumBins < 0) configNumBins = defaultNumBins;
                region->fillHistogram(newHistogram, histogramArray, configChannel, currStokes, configNumBins);
            }
        }
        histogramOK = true;
    } 
    return histogramOK;
}

bool Frame::fillSpatialProfileData(int regionId, CARTA::SpatialProfileData& profileData) {
    bool profileOK(false);
    if (regions.count(regionId)) {
        auto& region = regions[regionId];
        // set profile parameters
        std::vector<CARTA::Point> ctrlPts = region->getControlPoints();
        int x(ctrlPts[0].x()), y(ctrlPts[0].y());
        profileData.set_x(x);
        profileData.set_y(y);
        int chan(currentChannel()), stokes(currentStokes());
        profileData.set_channel(chan);
        profileData.set_stokes(stokes);
        casacore::uInt imSize(imageShape.size());
        casacore::IPosition valuePos(imSize);
        valuePos(0) = x;
        valuePos(1) = y;
        if (imSize>=3 && spectralAxis>=0)
            valuePos(spectralAxis) = chan;
        if (imSize>3 && stokesAxis>=0)
            valuePos(stokesAxis) = stokes;
        profileData.set_value(channelCache(valuePos));
        // set profiles
        for (size_t i=0; i<region->numSpatialProfiles(); ++i) {
            // SpatialProfile
            auto newProfile = profileData.add_profiles();
            newProfile->set_coordinate(region->getSpatialProfileStr(i));
            newProfile->set_start(0);
            // get <axis, stokes> for slicing image data
            std::pair<int,int> axisStokes = region->getSpatialProfileReq(i);
            std::vector<float> profile;
            if (axisStokes.second == stokes) {
                // use stored channel matrix 
                switch (axisStokes.first) {
                    case 0: { // x
                        profile = channelCache.column(y).tovector();
                        newProfile->set_end(imageShape(0));
                        break;
                    }
                    case 1: { // y
                        profile = channelCache.row(x).tovector();
                        newProfile->set_end(imageShape(1));
                        break;
                    }
                }
            } else {
                // slice image data
                casacore::Slicer section;
                switch (axisStokes.first) {
                    case 0: {  // x
                        getProfileSlicer(section, -1, y, chan, axisStokes.second);
                        newProfile->set_end(imageShape(0));
                        break;
                    }
                    case 1: { // y
                        getProfileSlicer(section, x, -1, chan, axisStokes.second);
                        newProfile->set_end(imageShape(1));
                        break;
                    }
                }
                casacore::Array<float> tmp;
                std::unique_lock<std::mutex> guard(mutex);
                loader->loadData(FileInfo::Data::XYZW).getSlice(tmp, section, true);
                profile = tmp.tovector();
            }
            *newProfile->mutable_values() = {profile.begin(), profile.end()};
        }
        profileOK = true;
    } 
    return profileOK;
}

bool Frame::fillSpectralProfileData(int regionId, CARTA::SpectralProfileData& profileData) {
    bool profileOK(false);
    if (regions.count(regionId)) {
        auto& region = regions[regionId];
        // set profile parameters
        int currStokes(currentStokes());
        profileData.set_stokes(currStokes);
        profileData.set_progress(1.0); // for now (cursor), send all at once
        // Set channel vals
        // get slicer
        std::vector<CARTA::Point> ctrlPts = region->getControlPoints();
        int x(ctrlPts[0].x()), y(ctrlPts[0].y());
        casacore::Slicer lattSlicer;
        getProfileSlicer(lattSlicer, x, y, -1, currStokes);
        // set stats profiles
        for (size_t i=0; i<region->numSpectralProfiles(); ++i) {
            // get sublattice for stokes requested in profile
            int profileStokes;
            if (region->getSpectralConfigStokes(profileStokes, i)) {
                if (profileStokes != currStokes)
                    getProfileSlicer(lattSlicer, x, y, -1, profileStokes);
                casacore::SubLattice<float> subLattice(loader->loadData(FileInfo::Data::XYZW), lattSlicer);
                std::unique_lock<std::mutex> guard(mutex);
                region->fillProfileStats(i, profileData, subLattice);
            }
        }
        profileOK = true;
    }
    return profileOK;
}

bool Frame::fillRegionStatsData(int regionId, CARTA::RegionStatsData& statsData) {
    bool statsOK(false);
    if (regions.count(regionId)) {
        auto& region = regions[regionId];
        if (region->numStats() > 0) {
            int currChan(currentChannel()), currStokes(currentStokes());
            statsData.set_channel(currChan);
            statsData.set_stokes(currStokes);
            casacore::Slicer lattSlicer;
            lattSlicer = getChannelMatrixSlicer(currChan, currStokes);  // for entire 2D image, for now
            casacore::SubLattice<float> subLattice(loader->loadData(FileInfo::Data::XYZW), lattSlicer);
            region->fillStatsData(statsData, subLattice);
            statsOK = true;
        }
    }
    return statsOK;
}

