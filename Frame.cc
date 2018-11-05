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
      loader(FileLoader::getLoader(filename)) {
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

        // determine chan and stokes axes
        stokesAxis = loader->stokesAxis();
        if (ndims < 3) chanAxis = -1;
        else chanAxis = 2;
        if (ndims == 4 && stokesAxis==2) chanAxis=3;

        log(uuid, "Opening image with dimensions: {}", imageShape);
        // string axesInfo = fmt::format("Opening image with dimensions: {}", dimensions);
        // sendLogEvent(axesInfo, {"file"}, CARTA::ErrorSeverity::DEBUG);

        // set current channel, stokes, channelCache
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
                            if (!isnan(pixVal)) {
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

    size_t depth(chanAxis>=0 ? imageShape(chanAxis) : 1);
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

    if (imageShape(1) < y + reqHeight || imageShape(0) < x + reqWidth) {
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
        size_t depth(chanAxis>=0 ? imageShape(chanAxis) : 1);
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
        if (!regions.count(CURSOR_REGION_ID)) {
            // create cursor region; also sets spatial reqs
            CARTA::Point centerPoint;
            centerPoint.set_x(imageShape(0)/2);
            centerPoint.set_y(imageShape(1)/2);
            setCursorRegion(CURSOR_REGION_ID, centerPoint);
        } else { // just set spatial reqs
            std::vector<std::string> spatialProfiles;
            setRegionSpatialRequirements(CURSOR_REGION_ID, spatialProfiles);
        }
    }

    // update Spectral requirements with current stokes
    if (stokesChanged) {
        if (!regions.count(CURSOR_REGION_ID)) {
            // create cursor region; also sets spectral reqs
            CARTA::Point centerPoint;
            centerPoint.set_x(imageShape(0)/2);
            centerPoint.set_y(imageShape(1)/2);
            setCursorRegion(CURSOR_REGION_ID, centerPoint);
        } else {
            std::vector<CARTA::SetSpectralRequirements_SpectralConfig> spectralProfiles;
            setRegionSpectralRequirements(CURSOR_REGION_ID, spectralProfiles);
        }
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
    loader->loadData(FileInfo::Data::XYZW).getSlice(tmp, section, true);
    chanMatrix.reference(tmp);
}

casacore::Slicer Frame::getChannelMatrixSlicer(size_t channel, size_t stokes) {
    casacore::IPosition count(2, imageShape(0), imageShape(1));
    casacore::IPosition start(2, 0, 0);

    // slice image data
    if(ndims == 3) {
        count.append(casacore::IPosition(1, 1));
        start.append(casacore::IPosition(1, channel));
    } else if (ndims == 4) {
        count.append(casacore::IPosition(2, 1, 1));
        if (stokesAxis==2)
            start.append(casacore::IPosition(2, stokes, channel));
        else
            start.append(casacore::IPosition(2, channel, stokes));
    }
    // slice image data
    casacore::Slicer section(start, count);
    return section;
}

void Frame::getProfileSlicer(casacore::Slicer& latticeSlicer, int x, int y, int channel, int stokes) {
    // to slice image data along x, y, or channel axis (indicated with -1)
    casacore::IPosition start, count;
    if (x<0) { // get x profile
        start = casacore::IPosition(1,0);
        count = casacore::IPosition(1,imageShape(0));
    } else {
        start = casacore::IPosition(1,x);
        count = casacore::IPosition(1,1);
    }

    if (y<0) { // get y profile
        start.append(casacore::IPosition(1,0));
        count.append(casacore::IPosition(1,imageShape(1)));
    } else {
        start.append(casacore::IPosition(1,y));
        count.append(casacore::IPosition(1,1));
    }

    if(ndims == 3) {
        if (channel<0) { // get spectral profile
            start.append(casacore::IPosition(1,0));
            count.append(casacore::IPosition(1,imageShape(2)));
        } else {
            start.append(casacore::IPosition(1,channel));
            count.append(casacore::IPosition(1,1));
        }
    } else if(ndims==4) {
        if (channel<0) { // get spectral profile with stokes
            if (stokesAxis==2) {
                start.append(casacore::IPosition(2,stokes,0));
                count.append(casacore::IPosition(2,1,imageShape(3)));
            } else {
                start.append(casacore::IPosition(2,0,stokes));
                count.append(casacore::IPosition(2,imageShape(2),1));
            }
        } else {
            if (stokesAxis==2)
                start.append(casacore::IPosition(2,stokes,channel));
            else
                start.append(casacore::IPosition(2,channel,stokes));
            count.append(casacore::IPosition(2,1,1));
        }
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

bool Frame::setRegion(int regionId, std::string name, CARTA::RegionType type, bool image) {
    // Create new Region and add to regions map.
    auto region = unique_ptr<carta::Region>(new carta::Region(name, type));
    // entire 2D image has regionId -1, but negative id also for creating new region
    if (regionId<0 && !image) { 
        // TODO: create regionId
    }
    regions[regionId] = move(region);
    return true;
}

bool Frame::setRegionChannels(int regionId, int minchan, int maxchan, std::vector<int>& stokes) {
    // set chans to current if -1; set stokes to current if empty
    if (regions.count(regionId)) {
        // validate
        int nchan(chanAxis>=0 ? imageShape(chanAxis) : 1);
        if (minchan > nchan || maxchan > nchan)
            return false;
        int nstokes(stokesAxis>=0 ? imageShape(stokesAxis) : 1);
        for (auto stoke : stokes)
            if (stoke > nstokes)
                return false;
        // set
        auto& region = regions[regionId];
        region->setChannels(minchan, maxchan, stokes);
        return true;
    } else {
        return false;
    }
}

bool Frame::setRegionControlPoints(int regionId, std::vector<CARTA::Point>& points) {
    // validate and set one or more control points for region 
    if (regions.count(regionId)) {
        // validate
        for (auto& point : points) {
            float x(point.x()), y(point.y());
            if ((x < 0 || x >= imageShape(0)) || (y < 0 || y >= imageShape(1)))
                return false;
        }
        // set
        auto& region = regions[regionId];
        region->setControlPoints(points);
        return true;
    } else {
        return false;
    }
}

bool Frame::setRegionRotation(int regionId, float rotation) {
    if (regions.count(regionId)) {
        auto& region = regions[regionId];
        region->setRotation(rotation);
        return true;
    } else {
        return false;
    }
}

// special cases of setRegion for image and cursor
void Frame::setImageRegion() {
    // create a Region for the entire image, regionId = -1
    setRegion(IMAGE_REGION_ID, "image", CARTA::RECTANGLE, true);

    // set image region channels: all channels, all stokes
    int minchan(0);
    int maxchan = (chanAxis>0 ? imageShape(chanAxis)-1 : 0);
    std::vector<int> stokes;
    if (stokesAxis > 0) {
        for (int i=0; i<imageShape(stokesAxis); ++i)
            stokes.push_back(i);
    }
    setRegionChannels(IMAGE_REGION_ID, minchan, maxchan, stokes);

    // control points: rectangle from top left (0,height) to bottom right (width,0)
    std::vector<CARTA::Point> points(2);
    CARTA::Point point;
    point.set_x(0);
    point.set_y(imageShape(1)); // height
    points.push_back(point);
    point.set_x(imageShape(0)); // width
    point.set_y(0);
    points.push_back(point);
    setRegionControlPoints(-1, points);

    // histogram requirements: use current channel
    std::vector<CARTA::SetHistogramRequirements_HistogramConfig> configs;
    setRegionHistogramRequirements(IMAGE_REGION_ID, configs);
}

bool Frame::setCursorRegion(int regionId, const CARTA::Point& point) {
    // a cursor is a region with one control point
    std::vector<CARTA::Point> points;
    points.push_back(point);

    if (regions.count(regionId)) {
        // update point
        return setRegionControlPoints(regionId, points);
    } else {
        // set up new region
        setRegion(regionId, "cursor", CARTA::POINT);
        // use current channel and stokes for cursor
        int chan(-1);
        std::vector<int> stokes;
        setRegionChannels(regionId, chan, chan, stokes);
        // control point is cursor position
        bool ok = setRegionControlPoints(regionId, points);
        // spatial requirements
        std::vector<std::string> spatialProfiles;
        setRegionSpatialRequirements(regionId, spatialProfiles);
        // spectral requirements
        std::vector<CARTA::SetSpectralRequirements_SpectralConfig> spectralProfiles;
        setRegionSpectralRequirements(regionId, spectralProfiles);
        return ok;
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
            config.set_channel(currentChannel());
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
    if (!regions.count(regionId) && regionId==0) {
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
    if (!regions.count(regionId) && regionId==0) {
        // in case frontend sends spectral reqs for cursor before SET_CURSOR; set cursor region
        CARTA::Point centerPoint;
        centerPoint.set_x(imageShape(0)/2);
        centerPoint.set_y(imageShape(1)/2);
        setCursorRegion(regionId, centerPoint);
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
    if (regions.count(regionId)) {
        auto& region = regions[regionId];
        region->setStatsRequirements(statsTypes);
        return true;
    } else {
        return false;
    }
}

// ***** region data *****

bool Frame::fillRegionHistogramData(int regionId, CARTA::RegionHistogramData* histogramData) {
    if (regions.count(regionId)) {
        auto& region = regions[regionId];
        histogramData->set_stokes(currentStokes());
        for (int i=0; i<region->numHistogramConfigs(); ++i) {
            CARTA::SetHistogramRequirements_HistogramConfig config = region->getHistogramConfig(i);
            int reqChannel(config.channel()), reqNumBins(config.num_bins());
            // get channel(s) and stokes
            int reqStokes(currentStokes());
            std::vector<int> reqChannels;
            if (reqChannel==-1) {
                reqChannels.push_back(currentChannel());
            } else if (reqChannel == -2) { // all channels
                for (size_t i=0; i<imageShape(chanAxis); ++i)
                   reqChannels.push_back(i);
            } else {
                reqChannels.push_back(reqChannel);
            }
            // fill Histograms
            for (auto channel : reqChannels) {
                auto newHistogram = histogramData->add_histograms();
                if ((!channelStats[reqStokes][channel].histogramBins.empty())) {
                    // use histogram from image file
                    auto& currentStats = channelStats[reqStokes][currentChannel()];
                    int nbins(currentStats.histogramBins.size());
                    newHistogram->set_num_bins(nbins);
                    newHistogram->set_bin_width((currentStats.maxVal - currentStats.minVal) / nbins);
                    newHistogram->set_first_bin_center(currentStats.minVal + (newHistogram->bin_width()/2.0));
                    *newHistogram->mutable_bins() = {currentStats.histogramBins.begin(), currentStats.histogramBins.end()};
                } else {
                    // get new or stored histogram from Region
                    casacore::Matrix<float> chanMatrix;
                    getChannelMatrix(chanMatrix, channel, reqStokes);
                    region->fillHistogram(newHistogram, chanMatrix, channel, reqStokes);
                }
            }
        }
        return true;
    } else {
        return false;
    }
}

bool Frame::fillSpatialProfileData(int regionId, CARTA::SpatialProfileData& profileData) {
    if (regions.count(regionId)) {
        auto& region = regions[regionId];
        // set profile parameters
        std::vector<CARTA::Point> ctrlPts = region->getControlPoints();
        int x(ctrlPts[0].x()), y(ctrlPts[0].y());
        profileData.set_x(x);
        profileData.set_y(y);
        int chan(currentChannel());
        profileData.set_channel(chan);
        profileData.set_stokes(currentStokes());
        profileData.set_value(channelCache(casacore::IPosition(2, x, y)));
        // set profiles
        for (size_t i=0; i<region->numSpatialProfiles(); ++i) {
            // SpatialProfile
            auto newProfile = profileData.add_profiles();
            newProfile->set_coordinate(region->getSpatialProfileStr(i));
            newProfile->set_start(0);
            // get <axis, stokes> for slicing image data
            std::pair<int,int> axisStokes = region->getSpatialProfileReq(i);
            std::vector<float> profile;
            if (axisStokes.second == currentStokes()) {
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
                loader->loadData(FileInfo::Data::XYZW).getSlice(tmp, section, true);
                profile = tmp.tovector();
            }
            *newProfile->mutable_values() = {profile.begin(), profile.end()};
        }
        return true;
    } else {
        return false;
    }
}

bool Frame::fillSpectralProfileData(int regionId, CARTA::SpectralProfileData& profileData) {
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
                region->fillProfileStats(i, profileData, subLattice);
            }
        }
        return true;
    } else {
        return false;
    }
}

bool Frame::fillRegionStatsData(int regionId, CARTA::RegionStatsData& statsData) {
    if (regions.count(regionId)) {
        auto& region = regions[regionId];
        int currChan(currentChannel()), currStokes(currentStokes());
        statsData.set_channel(currChan);
        statsData.set_stokes(currStokes);
        casacore::Slicer lattSlicer;
        lattSlicer = getChannelMatrixSlicer(currChan, currStokes);  // for entire 2D image, for now
        casacore::SubLattice<float> subLattice(loader->loadData(FileInfo::Data::XYZW), lattSlicer);
        region->fillStatsData(statsData, subLattice);
        return true;
    } else {
        return false;
    }
}

