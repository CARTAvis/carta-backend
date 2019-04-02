#include "Frame.h"
#include "compression.h"
#include "util.h"

#include <tbb/blocked_range2d.h>
#include <tbb/parallel_for.h>

using namespace carta;
using namespace std;

Frame::Frame(const string& uuidString, const string& filename, const string& hdu, int defaultChannel)
    : uuid(uuidString),
      valid(true),
      cursorSet(false),
      filename(filename),
      loader(FileLoader::getLoader(filename)),
      spectralAxis(-1), stokesAxis(-1),
      channelIndex(-1), stokesIndex(-1),
      nchan(1), nstok(1) {
    
    if (loader==nullptr) {
        log(uuid, "Problem loading file {}: loader not implemented", filename);
        valid = false;
        return;
    }
    
    try {
        loader->openFile(filename, hdu);
    } catch (casacore::AipsError& err) {
        log(uuid, "Problem loading file {}: {}", filename, err.getMesg());
        valid = false;
        return;
    }
        
    // Get shape and axis values from the loader
    if (!loader->findShape(imageShape, nchan, nstok, spectralAxis, stokesAxis)) {
        log(uuid, "Problem loading file {}: could not determine image shape", filename);
        valid = false;
        return;
    }

    // make Region for entire image (after current channel/stokes set)
    setImageRegion(IMAGE_REGION_ID);
    setDefaultCursor(); // frontend sets requirements for cursor before cursor set
    cursorSet = false;  // only true if set by frontend
    valid = true;

    // set current channel, stokes, imageCache
    channelIndex = defaultChannel;
    stokesIndex = DEFAULT_STOKES;
    setImageCache();

    try {
        // resize stats vectors and load data from image, if the format supports it
        // A failure here shouldn't invalidate the frame 
        loader->loadImageStats();
    } catch (casacore::AipsError& err) {
        log(uuid, "Problem loading statistics from file {}: {}", filename, err.getMesg());
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

std::vector<int> Frame::getRegionIds() {
    // return list of region ids for this frame
    std::vector<int> regionIDs;
    for (auto& region : regions) {
        regionIDs.push_back(region.first);
    }
    return regionIDs;
}

int Frame::getMaxRegionId() {
    std::vector<int> ids(getRegionIds());
    return *std::max_element(ids.begin(), ids.end());
}

size_t Frame::nchannels() {
    return nchan;
}

size_t Frame::nstokes() {
    return nstok;
}

int Frame::currentChannel() {
    return channelIndex;
}

int Frame::currentStokes() {
    return stokesIndex;
}

bool Frame::checkChannel(int channel) {
    return ((channel >= 0) && (channel < nchannels()));
}

bool Frame::checkStokes(int stokes) {
    return ((stokes >= 0) && (stokes < nstokes()));
}

// ********************************************************************
// Set regions

bool Frame::setRegion(int regionId, std::string name, CARTA::RegionType type,
        std::vector<CARTA::Point>& points, float rotation, std::string& message) {
    // Create or update Region
    bool regionSet(false);

    // create or update Region
    if (regions.count(regionId)) { // update Region
        auto& region = regions[regionId];
        regionSet = region->updateRegionParameters(name, type, points, rotation);
    }  else { // map new Region to region id
        auto region = unique_ptr<carta::Region>(new carta::Region(name, type, points, rotation,
            imageShape, spectralAxis, stokesAxis));
        if (region->isValid()) {
            regions[regionId] = move(region);
            regionSet = true;
        }
    }
    if (!regionSet) {
        message = fmt::format("Region parameters failed to validate for region id {}", regionId);
    }

    // set cursor's x-y coordinate cache
    if (name == "cursor" && type == CARTA::RegionType::POINT)
        cursorXY = std::make_pair(points[0].x(), points[0].y());

    return regionSet;
}

// special cases of setRegion for image and cursor
void Frame::setImageRegion(int regionId) {
    // Create a Region for the entire image plane: Image or Cube
    if ((regionId != IMAGE_REGION_ID) && (regionId != CUBE_REGION_ID))
        return;

    std::string name = (regionId == IMAGE_REGION_ID ? "image" : "cube");
    // control points: center pt [cx, cy], [width, height]
    std::vector<CARTA::Point> points(2);
    CARTA::Point point;
    point.set_x(imageShape(0)/2.0); // center x
    point.set_y(imageShape(1)/2.0); // center y
    points[0] = point;
    point.set_x(imageShape(0)); // entire width
    point.set_y(imageShape(1)); // entire height
    points[1] = point;
    // rotation
    float rotation(0.0);

    // create new region
    std::string message;
    bool ok = setRegion(regionId, name, CARTA::RECTANGLE, points, rotation, message);
    if (regionId == IMAGE_REGION_ID) { // set histogram requirements: use current channel
        CARTA::SetHistogramRequirements_HistogramConfig config;
        config.set_channel(CURRENT_CHANNEL);
        config.set_num_bins(AUTO_BIN_SIZE);
        std::vector<CARTA::SetHistogramRequirements_HistogramConfig> defaultConfigs(1, config);
        setRegionHistogramRequirements(IMAGE_REGION_ID, defaultConfigs);
    }
}

bool Frame::setCursorRegion(int regionId, const CARTA::Point& point) {
    // a cursor is a region with one control point and all channels for spectral profile
    std::vector<CARTA::Point> points(1, point);
    float rotation(0.0);
    std::string message;
    cursorSet = setRegion(regionId, "cursor", CARTA::POINT, points, rotation, message);
    return cursorSet;
}

void Frame::setDefaultCursor() {
    CARTA::Point defaultPoint;
    defaultPoint.set_x(0);
    defaultPoint.set_y(0);
    setCursorRegion(CURSOR_REGION_ID, defaultPoint);
    cursorSet = false;
}

bool Frame::regionChanged(int regionId) {
    bool changed(false);
    if (regions.count(regionId)) {
        auto& region = regions[regionId];
        changed = region->regionChanged();
    }
    return changed;
}

void Frame::removeRegion(int regionId) {
    if (regions.count(regionId)) {
        regions[regionId].reset();
        regions.erase(regionId);
    }
}

// ********************************************************************
// Image region parameters: view, channel/stokes, slicers

bool Frame::setImageView(const CARTA::ImageBounds& newBounds, int newMip,
        CARTA::CompressionType newCompression, float newQuality, int newSubsets) {
    // set image bounds and compression settings
    if (!valid) {
        return false;
    }
    const int xmin = newBounds.x_min();
    const int xmax = newBounds.x_max();
    const int ymin = newBounds.y_min();
    const int ymax = newBounds.y_max();
    const int reqHeight = ymax - ymin;
    const int reqWidth = xmax - xmin;

    // out of bounds check
    if ((reqHeight < 0) || (reqWidth < 0))
        return false;
    if ((imageShape(1) < ymin + reqHeight) || (imageShape(0) < xmin + reqWidth))
        return false;
    if (newMip < 0)
        return false;

    bounds = newBounds;
    mip = newMip;
    compType = newCompression;
    quality = newQuality;
    nsubsets = newSubsets;
    return true;
}

bool Frame::setImageChannels(int newChannel, int newStokes, std::string& message) {
    bool updated(false);
    if (!valid || (regions.count(IMAGE_REGION_ID)==0)) {
        message = "No file loaded";
    } else {
        if ((newChannel != channelIndex) || (newStokes != stokesIndex)) {
            auto& region = regions[IMAGE_REGION_ID];
            bool chanOK(checkChannel(newChannel));
            bool stokesOK(checkStokes(newStokes));
            if (chanOK && stokesOK) {
                channelIndex = newChannel;
                stokesIndex = newStokes;
                setImageCache();
                updated = true;
            } else {
                message = fmt::format("Channel {} or Stokes {} is invalid in file {}", newChannel,
                    newStokes, filename);
            }
        }
    }
    return updated;
}

void Frame::setImageCache() {
    // get image data for channel, stokes
    bool writeLock(true);
    tbb::queuing_rw_mutex::scoped_lock cacheLock(cacheMutex, writeLock);
    imageCache.resize(imageShape(0) * imageShape(1));
    casacore::Slicer section = getChannelMatrixSlicer(channelIndex, stokesIndex);
    casacore::Array<float> tmp(section.length(), imageCache.data(), casacore::StorageInitPolicy::SHARE);
    std::lock_guard<std::mutex> guard(latticeMutex);
    loader->loadData(FileInfo::Data::XYZW).getSlice(tmp, section, true);
}

void Frame::getChannelMatrix(std::vector<float>& chanMatrix, size_t channel, size_t stokes) {
    // fill matrix for given channel and stokes
    casacore::Slicer section = getChannelMatrixSlicer(channel, stokes);
    chanMatrix.resize(imageShape(0) * imageShape(1));
    casacore::Array<float> tmp(section.length(), chanMatrix.data(), casacore::StorageInitPolicy::SHARE);
    // slice image data
    std::unique_lock<std::mutex> guard(latticeMutex);
    loader->loadData(FileInfo::Data::XYZW).getSlice(tmp, section, true);
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

void Frame::getLatticeSlicer(casacore::Slicer& latticeSlicer, int x, int y, int channel, int stokes) {
    // to slice image data along axes (full axis indicated with -1)
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

bool Frame::getRegionSubLattice(int regionId, casacore::SubLattice<float>& sublattice, int stokes,
        int channel) {
    // Apply lattice region to lattice and return SubLattice. Applies lattice mask to SubLattice.
    // channel could be ALL_CHANNELS in region channel range (default) or
    // a given channel (e.g. current channel).
    // Returns false if lattice region is invalid and cannot make sublattice.
    bool sublatticeOK(false);
    if (checkStokes(stokes) && (regions.count(regionId))) {
        auto& region = regions[regionId];
        if (region->isValid()) {
            casacore::LatticeRegion lattRegion;
            if (region->getRegion(lattRegion, stokes, channel)) {
                sublattice = casacore::SubLattice<float>(loader->loadData(FileInfo::Data::XYZW), lattRegion);
                if (!sublattice.hasPixelMask()) { // apply lattice mask if possible
                    casacore::Array<bool> maskArray;
                    if (loader->hasData(FileInfo::Data::Mask)) {
                        // apply region slicer to lattice pixel mask - same shape as sublattice
                        loader->getPixelMaskSlice(maskArray, lattRegion.slicer());
                    } else {
                        generatePixelMask(maskArray, sublattice);
                    }
                    casacore::ArrayLattice<bool> pixelMask(maskArray);
                    sublattice.setPixelMask(pixelMask, false);
                }
                sublatticeOK = true;
            }
        }
    }
    return sublatticeOK;
}

void Frame::generatePixelMask(casacore::Array<bool>& maskArray, casacore::SubLattice<float>& sublattice) {
    // Create boolean Array which is false for NaN values in sublattice
    std::vector<float> regionData;
    getSpectralData(regionData, sublattice);
    size_t dataSize(regionData.size());
    casacore::Vector<bool> maskData(dataSize);
    for (size_t i=0; i<dataSize; ++i)
        maskData[i] = isfinite(regionData[i]);
    casacore::Array<bool> mask(sublattice.shape(), maskData.data());
    maskArray.reference(mask);
}

// ****************************************************
// Region requirements

bool Frame::setRegionHistogramRequirements(int regionId,
        const std::vector<CARTA::SetHistogramRequirements_HistogramConfig>& histograms) {
    // set channel and num_bins for required histograms
    bool regionOK(false);
    if ((regionId == CUBE_REGION_ID) && (!regions.count(regionId)))
        setImageRegion(CUBE_REGION_ID); // create this region
    if (regions.count(regionId)) {
        auto& region = regions[regionId];
        regionOK = region->setHistogramRequirements(histograms);
    }
    return regionOK;
}

bool Frame::setRegionSpatialRequirements(int regionId, const std::vector<std::string>& profiles) {
    // set requested spatial profiles e.g. ["Qx", "Uy"] or just ["x","y"] to use current stokes
    bool regionOK(false);
    if (regions.count(regionId)) {
        auto& region = regions[regionId];
        regionOK = region->setSpatialRequirements(profiles, nstokes());
    }
    return regionOK;
}

bool Frame::setRegionSpectralRequirements(int regionId,
        const std::vector<CARTA::SetSpectralRequirements_SpectralConfig>& profiles) {
    // set requested spectral profiles e.g. ["Qz", "Uz"] or just ["z"] to use current stokes
    bool regionOK(false);
    if (regions.count(regionId)) {
        auto& region = regions[regionId];
        regionOK = region->setSpectralRequirements(profiles, nstokes());
    }
    return regionOK;
}

bool Frame::setRegionStatsRequirements(int regionId, const std::vector<int> statsTypes) {
    bool regionOK(false);
    if (regions.count(regionId)) {
        auto& region = regions[regionId];
        region->setStatsRequirements(statsTypes);
        regionOK = true;
    }
    return regionOK;
}


// ****************************************************
// Data for Image region

bool Frame::fillRasterImageData(CARTA::RasterImageData& rasterImageData, std::string& message) {
    // fill data message with compressed channel cache data
    bool rasterDataOK(false);
    std::vector<float> imageData;
    if (getImageData(imageData)) {
        rasterImageData.mutable_image_bounds()->set_x_min(bounds.x_min());
        rasterImageData.mutable_image_bounds()->set_x_max(bounds.x_max());
        rasterImageData.mutable_image_bounds()->set_y_min(bounds.y_min());
        rasterImageData.mutable_image_bounds()->set_y_max(bounds.y_max());
        rasterImageData.set_channel(channelIndex);
        rasterImageData.set_stokes(stokesIndex);
        rasterImageData.set_mip(mip);
        if (compType == CARTA::CompressionType::NONE) {
            rasterImageData.set_compression_type(CARTA::CompressionType::NONE);
            rasterImageData.set_compression_quality(0);
            rasterImageData.add_image_data(imageData.data(), imageData.size() * sizeof(float));
            rasterDataOK = true;
        } else if (compType == CARTA::CompressionType::ZFP) {
            rasterImageData.set_compression_type(CARTA::CompressionType::ZFP);
            int precision = lround(quality);
            rasterImageData.set_compression_quality(precision);

            auto rowLength = (bounds.x_max() - bounds.x_min()) / mip;
            auto numRows = (bounds.y_max() - bounds.y_min()) / mip;
            std::vector<std::vector<char>> compressionBuffers(nsubsets);
            std::vector<size_t> compressedSizes(nsubsets);
            std::vector<std::vector<int32_t>> nanEncodings(nsubsets);

            auto N = std::min(nsubsets, MAX_SUBSETS);
            auto range = tbb::blocked_range<int>(0, N);
            auto loop = [&](const tbb::blocked_range<int> &r) {
                for(int i = r.begin(); i != r.end(); ++i) {
                    int subsetRowStart = i * (numRows / N);
                    int subsetRowEnd = (i + 1) * (numRows / N);
                    if (i == N - 1) {
                        subsetRowEnd = numRows;
                    }
                    int subsetElementStart = subsetRowStart * rowLength;
                    int subsetElementEnd = subsetRowEnd * rowLength;
                    nanEncodings[i] = getNanEncodingsBlock(imageData, subsetElementStart, rowLength,
                        subsetRowEnd - subsetRowStart);
                    compress(imageData, subsetElementStart, compressionBuffers[i], compressedSizes[i],
                        rowLength, subsetRowEnd - subsetRowStart, precision);
                }
            };
            tbb::parallel_for(range, loop);

            // Complete message
            for (auto i = 0; i < nsubsets; i++) {
                rasterImageData.add_image_data(compressionBuffers[i].data(), compressedSizes[i]);
                rasterImageData.add_nan_encodings((char*) nanEncodings[i].data(),
                    nanEncodings[i].size() * sizeof(int));
            }
            rasterDataOK = true;
        } else {
            message = "SZ compression not implemented";
        }
    } else {
        message = "Raster image data failed to load";
    }
    return rasterDataOK;
}

bool Frame::getImageData(std::vector<float>& imageData, bool meanFilter) {
    // apply bounds and downsample image cache
    if (!valid || imageCache.empty()) {
        return false;
    }

    const int x = bounds.x_min();
    const int y = bounds.y_min();
    const int reqHeight = bounds.y_max() - y;
    const int reqWidth = bounds.x_max() - x;

    // check bounds
    if ((reqHeight < 0) || (reqWidth < 0))
        return false;
    if (imageShape(1) < y + reqHeight || imageShape(0) < x + reqWidth)
        return false;
    // check mip; cannot divide by zero
    if (mip < 0)
        return false;

    // size returned vector
    size_t numRowsRegion = reqHeight / mip;
    size_t rowLengthRegion = reqWidth / mip;
    imageData.resize(numRowsRegion * rowLengthRegion);
    int nImgCol = imageShape(0);

    // read lock imageCache
    bool writeLock(false);
    tbb::queuing_rw_mutex::scoped_lock lock(cacheMutex, writeLock);

    if (meanFilter) {
        // Perform down-sampling by calculating the mean for each MIPxMIP block
        auto range = tbb::blocked_range2d<size_t>(0, numRowsRegion, 0, rowLengthRegion);
        auto loop = [&](const tbb::blocked_range2d<size_t>& r) {
            for (size_t j = r.rows().begin(); j != r.rows().end(); ++j) {
                for (size_t i = r.cols().begin(); i != r.cols().end(); ++i) {
                    float pixelSum = 0;
                    int pixelCount = 0;
                    size_t imageRow = y + j * mip;
                    for (size_t pixelY = 0; pixelY < mip; pixelY++) {
                        size_t imageCol = x + i * mip;
                        for (size_t pixelX = 0; pixelX < mip; pixelX++) {
                            float pixVal = imageCache[(imageRow * nImgCol) + imageCol];
                            if (isfinite(pixVal)) {
                                pixelCount++;
                                pixelSum += pixVal;
                            }
                            imageCol++;
                        }
                        imageRow++;
                    }
                    imageData[j * rowLengthRegion + i] = pixelCount ? pixelSum / pixelCount : NAN;
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
                    imageData[j * rowLengthRegion + i] = imageCache[(imageRow * nImgCol) + imageCol];
                }
            }
        };
        tbb::parallel_for(range, loop);
    }
    return true;
}


// ****************************************************
// Region histograms, profiles, stats

bool Frame::fillRegionHistogramData(int regionId, CARTA::RegionHistogramData* histogramData,
        bool checkCurrentChannel) {
    // fill histogram message with histograms for requested channel/num bins
    bool histogramOK(false);
    if (regions.count(regionId)) {
        auto& region = regions[regionId];
        size_t numHistograms(region->numHistogramConfigs());
        if (numHistograms==0)
            return false; // not requested

        int currStokes(currentStokes());
        histogramData->set_stokes(currStokes);
        histogramData->set_progress(1.0); // send entire histogram
        for (size_t i=0; i<numHistograms; ++i) {
            // get histogram requirements for this index
            CARTA::SetHistogramRequirements_HistogramConfig config = region->getHistogramConfig(i);
            int configChannel(config.channel()), configNumBins(config.num_bins());
            // only send if using current stokes, which changed
            if (checkCurrentChannel && (configChannel != CURRENT_CHANNEL))
                return false;
            if (configChannel == CURRENT_CHANNEL) {
                configChannel = channelIndex;
            }
            auto newHistogram = histogramData->add_histograms();
            newHistogram->set_channel(configChannel);
            // get stored histograms or fill new histograms
            auto& currentStats = loader->getImageStats(currStokes, configChannel);
            bool haveHistogram(false);

            // Check if read from image file (HDF5 only)
            if (currentStats.valid) {
                int nbins(currentStats.histogramBins.size());
                if ((configNumBins == AUTO_BIN_SIZE) || (configNumBins == nbins)) {
                    newHistogram->set_num_bins(nbins);
                    newHistogram->set_bin_width((currentStats.maxVal - currentStats.minVal) / nbins);
                    newHistogram->set_first_bin_center(currentStats.minVal + (newHistogram->bin_width()/2.0));
                    *newHistogram->mutable_bins() = {currentStats.histogramBins.begin(),
                        currentStats.histogramBins.end()};
                    haveHistogram = true;
                }
            }
            if (!haveHistogram) {
                // Retrieve histogram if stored
                if (!getRegionHistogram(regionId, configChannel, currStokes, configNumBins, *newHistogram)) {
                    // Calculate histogram
                    float minval, maxval;
                    if (!getRegionMinMax(regionId, configChannel, currStokes, minval, maxval)) {
                        calcRegionMinMax(regionId, configChannel, currStokes, minval, maxval);
                    }
                    calcRegionHistogram(regionId, configChannel, currStokes, configNumBins, minval, maxval,
                        *newHistogram);
                }
            }
        }
        histogramOK = true;
    }
    return histogramOK;
}

bool Frame::fillSpatialProfileData(int regionId, CARTA::SpatialProfileData& profileData,
        bool checkCurrentStokes) {
    // fill spatial profile message with requested x/y profiles (for a point region)
    bool profileOK(false);
    if (regions.count(regionId)) {
        auto& region = regions[regionId];
        if (!region->isValid() || !region->isPoint())
            return profileOK;
        size_t numProfiles(region->numSpatialProfiles());
        if (numProfiles==0)
            return profileOK; // not requested

        // set spatial profile fields
        std::vector<CARTA::Point> ctrlPts = region->getControlPoints();
        int x(static_cast<int>(std::round(ctrlPts[0].x()))),
            y(static_cast<int>(std::round(ctrlPts[0].y())));
        // check that control points in image
        bool pointInImage((x >= 0) && (x < imageShape(0)) && (y >= 0) && (y < imageShape(1)));
        if (pointInImage) {
            ssize_t nImageCol(imageShape(0)), nImageRow(imageShape(1));
            float value(0.0);
            if (!imageCache.empty()) {
                bool writeLock(false);
                tbb::queuing_rw_mutex::scoped_lock cacheLock(cacheMutex, writeLock);
                value = imageCache[(y*nImageCol) + x];
                cacheLock.release();
            }
            profileData.set_x(x);
            profileData.set_y(y);
            profileData.set_channel(channelIndex);
            profileData.set_stokes(stokesIndex);
            profileData.set_value(value);

            // set profiles
            for (size_t i=0; i<numProfiles; ++i) {
                // get <axis, stokes> for slicing image data
                std::pair<int,int> axisStokes = region->getSpatialProfileReq(i);
                // only send if using current stokes, which changed
                if (checkCurrentStokes && (axisStokes.second != CURRENT_STOKES))
                    return false;

                int profileStokes = (axisStokes.second < 0 ? stokesIndex : axisStokes.second);
                std::vector<float> profile;
                int end(0);
                if ((profileStokes == stokesIndex) && !imageCache.empty()) {
                    // use stored channel cache
                    bool writeLock(false);
                    switch (axisStokes.first) {
                        case 0: { // x
                            tbb::queuing_rw_mutex::scoped_lock cacheLock(cacheMutex, writeLock);
                            auto xStart = y * nImageCol;
                            profile.reserve(imageShape(0));
                            for (unsigned int i=0; i<imageShape(0); ++i) {
                                auto idx = xStart + i;
                                profile.push_back(imageCache[idx]);
                            }
                            cacheLock.release();
                            end = imageShape(0);
                            break;
                        }
                        case 1: { // y
                            tbb::queuing_rw_mutex::scoped_lock cacheLock(cacheMutex, writeLock);
                            profile.reserve(imageShape(1));
                            for (unsigned int i=0; i<imageShape(1); ++i) {
                                auto idx = (i * nImageCol) + x;
                                profile.push_back(imageCache[idx]);
                            }
                            cacheLock.release();
                            end = imageShape(1);
                            break;
                        }
                    }
                } else {
                    // slice image data
                    casacore::Slicer section;
                    switch (axisStokes.first) {
                        case 0: {  // x
                            getLatticeSlicer(section, -1, y, channelIndex, profileStokes);
                            end = imageShape(0);
                            break;
                        }
                        case 1: { // y
                            getLatticeSlicer(section, x, -1, channelIndex, profileStokes);
                            end = imageShape(1);
                            break;
                        }
                    }
                    profile.resize(end);
                    casacore::Array<float> tmp(section.length(), profile.data(), casacore::StorageInitPolicy::SHARE);
                    std::lock_guard<std::mutex> guard(latticeMutex);
                    loader->loadData(FileInfo::Data::XYZW).getSlice(tmp, section, true);
                }
                // SpatialProfile
                auto newProfile = profileData.add_profiles();
                newProfile->set_coordinate(region->getSpatialCoordinate(i));
                newProfile->set_start(0);
                newProfile->set_end(end);
                *newProfile->mutable_values() = {profile.begin(), profile.end()};
            }
            profileOK = true;
        }
    }
    return profileOK;
}

bool Frame::fillSpectralProfileData(int regionId, CARTA::SpectralProfileData& profileData,
        bool checkCurrentStokes) {
    // fill spectral profile message with requested statistics (or values for a point region)
    bool profileOK(false);
    if (regions.count(regionId)) {
        auto& region = regions[regionId];
        if (!region->isValid())
            return false;
        size_t numProfiles(region->numSpectralProfiles());
        if (numProfiles == 0)
            return false; // not requested

        // set profile parameters
        int currStokes(currentStokes());
        profileData.set_stokes(currStokes);
        profileData.set_progress(1.0); // send profile and stats together
        // set stats profiles
        for (size_t i=0; i<numProfiles; ++i) {
            int profileStokes;
            if (region->getSpectralConfigStokes(profileStokes, i)) {
                // only send if using current stokes, which changed
                if (checkCurrentStokes && (profileStokes != CURRENT_STOKES))
                    return false;
                if (profileStokes == CURRENT_STOKES)
                    profileStokes = currStokes;
                // get sublattice for stokes requested in profile
                casacore::SubLattice<float> sublattice;
                std::unique_lock<std::mutex> guard(latticeMutex);
                if (getRegionSubLattice(regionId, sublattice, profileStokes)) {
                    // fill SpectralProfiles for this config
                    if (region->isPoint()) {  // values
                        std::vector<float> spectralData;
                        getSpectralData(spectralData, sublattice, 100);
                        guard.unlock();
                        region->fillSpectralProfileData(profileData, i, spectralData);
                    } else {  // statistics
                        region->fillSpectralProfileData(profileData, i, sublattice);
                        guard.unlock();
                    }
                }
            }
        }
        profileOK = true;
    }
    return profileOK;
}

bool Frame::fillRegionStatsData(int regionId, CARTA::RegionStatsData& statsData) {
    // fill stats data message with requested statistics for the region with
    // current channel and stokes
    bool statsOK(false);
    if (regions.count(regionId)) {
        auto& region = regions[regionId];
        if (!region->isValid())
            return false;
        if (region->numStats() == 0)
            return false; // not requested

        statsData.set_channel(channelIndex);
        statsData.set_stokes(stokesIndex);
        casacore::SubLattice<float> sublattice;
        std::lock_guard<std::mutex> guard(latticeMutex);
        // current channel only
        if (getRegionSubLattice(regionId, sublattice, stokesIndex, channelIndex)) {
            region->fillStatsData(statsData, sublattice);
            statsOK = true;
        }
    }
    return statsOK;
}

// ****************************************************
// Region histograms only (not full data message)

int Frame::calcAutoNumBins(int regionId) {
    // automatic bin size for histogram when num_bins == AUTO_BIN_SIZE 
    int autoNumBins(int(max(sqrt(imageShape(0) * imageShape(1)), 2.0))); //default: use image plane
    if ((regionId != IMAGE_REGION_ID) && (regionId != CUBE_REGION_ID)) {
        if (regions.count(regionId)) {
            auto& region = regions[regionId];
            casacore::IPosition regionShape(region->xyShape()); // bounding box
            if (regionShape.size() > 0) {
                autoNumBins = (int(max(sqrt(regionShape(0) * regionShape(1)), 2.0)));
            }
        }
    }
    return autoNumBins;
}

bool Frame::getRegionMinMax(int regionId, int channel, int stokes, float& minval, float& maxval) {
    // Return stored min and max value; false if not stored
    bool haveMinMax(false);
    if (regions.count(regionId)) {
        auto& region = regions[regionId];
        haveMinMax = region->getMinMax(channel, stokes, minval, maxval);
    } 
    return haveMinMax;
}

bool Frame::calcRegionMinMax(int regionId, int channel, int stokes, float& minval, float& maxval) {
    // Calculate and store min/max for region data
    bool minmaxOK(false);
    if (regions.count(regionId)) {
        auto& region = regions[regionId];
        if (((regionId == IMAGE_REGION_ID) || (regionId == CUBE_REGION_ID)) &&
            (channel == channelIndex)) {  // use channel cache
            bool writeLock(false);
            tbb::queuing_rw_mutex::scoped_lock cacheLock(cacheMutex, writeLock);
            region->calcMinMax(channel, stokes, imageCache, minval, maxval);
            minmaxOK = true;
        } else {
            std::unique_lock<std::mutex> guard(latticeMutex);
            casacore::SubLattice<float> sublattice;
            getRegionSubLattice(regionId, sublattice, stokes, channel);
            std::vector<float> regionData;
            bool hasData(region->getData(regionData, sublattice));
            guard.unlock();
            if (hasData) {
                region->calcMinMax(channel, stokes, regionData, minval, maxval);
            }
            minmaxOK = hasData;
        }
    }
    return minmaxOK;
} 

bool Frame::getRegionHistogram(int regionId, int channel, int stokes, int nbins,
    CARTA::Histogram& histogram) {
    // Return stored histogram in histogram parameter
    bool haveHistogram(false);
    if (regions.count(regionId)) {
        auto& region = regions[regionId];
        nbins = (nbins==AUTO_BIN_SIZE ? calcAutoNumBins(regionId) : nbins);
        haveHistogram = region->getHistogram(channel, stokes, nbins, histogram);
    }
    return haveHistogram;
}

bool Frame::calcRegionHistogram(int regionId, int channel, int stokes, int nbins,
    float minval, float maxval, CARTA::Histogram& histogram) {
    // Return calculated histogram in histogram parameter
    bool histogramOK(false);
    if (regions.count(regionId)) {
        auto& region = regions[regionId];
        nbins = (nbins==AUTO_BIN_SIZE ? calcAutoNumBins(regionId) : nbins);
        if (regionId == IMAGE_REGION_ID) {
            if (channel == channelIndex) {  // use channel cache
                bool writeLock(false);
                tbb::queuing_rw_mutex::scoped_lock cacheLock(cacheMutex, writeLock);
                region->calcHistogram(channel, stokes, nbins, minval, maxval, imageCache, histogram);
            } else {
                std::vector<float> data;
                getChannelMatrix(data, channel, stokes);
                region->calcHistogram(channel, stokes, nbins, minval, maxval, data, histogram);
            }
            histogramOK = true;
        } else {
            std::unique_lock<std::mutex> guard(latticeMutex);
            casacore::SubLattice<float> sublattice;
            getRegionSubLattice(regionId, sublattice, stokes, channel);
            std::vector<float> regionData;
            bool hasData(region->getData(regionData, sublattice));
            guard.unlock();
            if (hasData) {
                region->calcHistogram(channel, stokes, nbins, minval, maxval, regionData, histogram);
            }
            histogramOK = hasData;
        }
    }
    return histogramOK;
}

// store cube histogram calculations
void Frame::setRegionMinMax(int regionId, int channel, int stokes, float minval, float maxval) {
    // Store cube min/max calculated in Session
    if (!regions.count(regionId) && (regionId==CUBE_REGION_ID))
        setImageRegion(CUBE_REGION_ID);

    if (regions.count(regionId)) {
        auto& region = regions[regionId];
        region->setMinMax(channel, stokes, minval, maxval);
    }
}

void Frame::setRegionHistogram(int regionId, int channel, int stokes, CARTA::Histogram& histogram) {
    // Store cube histogram calculated in Session
    if (!regions.count(regionId) && (regionId==CUBE_REGION_ID))
        setImageRegion(CUBE_REGION_ID);

    if (regions.count(regionId)) {
        auto& region = regions[regionId];
        region->setHistogram(channel, stokes, histogram);
    }
}

bool Frame::getSublatticeXY(casacore::SubLattice<float>& sublattice, std::pair<int, int>& cursor_xy) {
    bool result(false);
    casacore::IPosition sublattShape = sublattice.shape();
    casacore::IPosition start(sublattShape.size(), 0);
    casacore::IPosition count(sublattShape);
    if (count(0) == 1 && count(1) == 1) { // make sure the sub-lattice is a point region in x-y plane
        casacore::IPosition pos = sublattice.positionInParent(start);
        cursor_xy = std::make_pair(pos(0), pos(1));
        result = true;
    }
    return result;
}

bool Frame::getSpectralData(std::vector<float>& data, casacore::SubLattice<float>& sublattice, int checkPerChannels) {
    bool dataOK(false);
    casacore::IPosition sublattShape = sublattice.shape();
    data.resize(sublattShape.product());
    if (checkPerChannels > 0 && sublattShape.size() > 2) { // stoppable spectral profile process
        try {
            casacore::IPosition start(sublattShape.size(), 0);
            casacore::IPosition count(sublattShape);
            int profileAxis; // profile axis index
            size_t profileSize; // profile vector size
            // get profile axis index and its vector size
            for (int i=0; i<sublattShape.size(); ++i) {
                if (count(i)>1) {
                    profileAxis = i;
                    profileSize = count(i);
                }
            }
            size_t begin = 0; // the begin index of profile vector in each copy
            size_t upperBound = (profileSize%checkPerChannels == 0 ? profileSize/checkPerChannels : profileSize/checkPerChannels + 1);
            // get cursor's x-y coordinate from sub-lattice
            std::pair<int, int> tmpXY;
            getSublatticeXY(sublattice, tmpXY);
            // get profile data section by section with a specific length (i.e., checkPerChannels)
            for (size_t i=0; i<upperBound; ++i) {
                // check if cursor's position changed during this loop, if so, stop the profile process
                if (tmpXY != cursorXY)
                    return false;
                // modify the start position for slicer
                start(profileAxis) = i*checkPerChannels;
                // modify the count for slicer
                count(profileAxis) = (checkPerChannels*(i+1) < profileSize ? checkPerChannels : profileSize - i*checkPerChannels);
                casacore::Slicer slicer(start, count);
                casacore::Array<float> buffer;
                sublattice.doGetSlice(buffer, slicer);
                memcpy(&data[begin], buffer.data(), count(profileAxis)*sizeof(float));
                begin += count(profileAxis);
            }
            dataOK = true;
        } catch (casacore::AipsError& err) {
        }
    } else { // non-stoppable spectral profile process
        casacore::Array<float> tmp(sublattShape, data.data(), casacore::StorageInitPolicy::SHARE);
        try {
            sublattice.doGetSlice(tmp, casacore::Slicer(casacore::IPosition(sublattShape.size(), 0), sublattShape));
            dataOK = true;
        } catch (casacore::AipsError& err) {
        }
    }
    return dataOK;
}
