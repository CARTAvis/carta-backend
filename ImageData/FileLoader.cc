#include "CasaLoader.h"
#include "FileLoader.h"
#include "HDF5Loader.h"
#include "FITSLoader.h"
#include "MIRIADLoader.h"

using namespace carta;

FileLoader* FileLoader::getLoader(const std::string &filename) {
    casacore::ImageOpener::ImageTypes type = FileInfo::fileType(filename);
    switch(type) {
    case casacore::ImageOpener::AIPSPP:
        return new CasaLoader(filename);
    case casacore::ImageOpener::FITS:
        return new FITSLoader(filename);
        break;
    case casacore::ImageOpener::MIRIAD:
        return new MIRIADLoader(filename);
        break;
    case casacore::ImageOpener::GIPSY:
        break;
    case casacore::ImageOpener::CAIPS:
        break;
    case casacore::ImageOpener::NEWSTAR:
        break;
    case casacore::ImageOpener::HDF5:
        return new HDF5Loader(filename);
	break;
    case casacore::ImageOpener::IMAGECONCAT:
        break;
    case casacore::ImageOpener::IMAGEEXPR:
        break;
    case casacore::ImageOpener::COMPLISTIMAGE:
        break;
    default:
        break;
    }
    return nullptr;
}

void FileLoader::findCoords(int& spectralAxis, int& stokesAxis) {
    // use CoordinateSystem to determine axis coordinate types
    spectralAxis = -1;
    stokesAxis = -1; 
    const casacore::CoordinateSystem csys(getCoordSystem());
    // spectral axis
    spectralAxis = casacore::CoordinateUtil::findSpectralAxis(csys);
    if (spectralAxis < 0) {
        int tabCoord = csys.findCoordinate(casacore::Coordinate::TABULAR);
        if (tabCoord >= 0) {
            casacore::Vector<casacore::Int> pixelAxes = csys.pixelAxes(tabCoord);
            for (casacore::uInt i=0; i<pixelAxes.size(); ++i) {
                casacore::String axisName = csys.worldAxisNames()(pixelAxes(i));
                if (axisName == "Frequency" || axisName == "Velocity")
                    spectralAxis = pixelAxes(i);
            }
        }
    }
    // stokes axis
    int pixel, world, coord;
    casacore::CoordinateUtil::findStokesAxis(pixel, world, coord, csys);
    if (coord >= 0) stokesAxis = pixel;

    // not found!
    if (spectralAxis < 2) { // spectral not found or is xy
        if (stokesAxis < 2) {  // stokes not found or is xy, use defaults
            spectralAxis = 2;
            stokesAxis = 3;
        } else { // stokes found, set spectral to other one
            if (stokesAxis==2) spectralAxis = 3;
            else spectralAxis = 2;
        }
    } else if (stokesAxis < 2) {  // stokes not found
        // set stokes to the other one
        if (spectralAxis == 2) stokesAxis = 3;
        else stokesAxis = 2;
    }
}

bool FileLoader::findShape(ipos& shape, size_t& nchannels, size_t& nstokes, int& spectralAxis, int& stokesAxis) {
    auto &image = loadData(FileInfo::Data::Image);
    
    shape = image.shape();
    size_t ndims = shape.size();
    
    if (ndims < 2 || ndims > 4) {
        return false;
    }

    // determine axis order (0-based)
    if (ndims == 3) { // use defaults
        spectralAxis = 2;
        stokesAxis = -1;
    } else if (ndims == 4) {  // find spectral and stokes axes
        findCoords(spectralAxis, stokesAxis);
    }
    
    nchannels = (spectralAxis>=0 ? shape(spectralAxis) : 1);
    nstokes = (stokesAxis>=0 ? shape(stokesAxis) : 1);
    
    this->ndims = ndims;
    this->nchannels = nchannels;
    this->nstokes = nstokes;
    
    return true;
}

const FileLoader::ipos FileLoader::getStatsDataShape(FileInfo::Data ds) {
    throw casacore::AipsError("getStatsDataShape not implemented in this loader");
}

casacore::ArrayBase* FileLoader::getStatsData(FileInfo::Data ds) {
    throw casacore::AipsError("getStatsData not implemented in this loader");
}

void FileLoader::loadStats2DBasic(FileInfo::Data ds) {
    if (hasData(ds)) {
        const ipos& statDims = getStatsDataShape(ds);
        
        // We can handle 2D, 3D and 4D in the same way
        if ((ndims == 2 && statDims.size() == 0)
            || (ndims == 3 && statDims.isEqual(ipos(1, nchannels)))
            || (ndims == 4 && statDims.isEqual(ipos(2, nchannels, nstokes)))) {
            
            auto data = getStatsData(ds);
            
            switch(ds) {
                case FileInfo::Data::S2DMax: {
                    auto it = static_cast<casacore::Array<casacore::Float>*>(data)->begin();
                    for (size_t s = 0; s < nstokes; s++) {
                        for (size_t c = 0; c < nchannels; c++) {
                            channelStats[s][c].maxVal = *it++;
                        }
                    }
                    break;
                }
                case FileInfo::Data::S2DMin: {
                    auto it = static_cast<casacore::Array<casacore::Float>*>(data)->begin();
                    for (size_t s = 0; s < nstokes; s++) {
                        for (size_t c = 0; c < nchannels; c++) {
                            channelStats[s][c].minVal = *it++;
                        }
                    }
                    break;
                }
                case FileInfo::Data::S2DMean: {
                    auto it = static_cast<casacore::Array<casacore::Float>*>(data)->begin();
                    for (size_t s = 0; s < nstokes; s++) {
                        for (size_t c = 0; c < nchannels; c++) {
                            channelStats[s][c].mean = *it++;
                        }
                    }
                    break;
                }
                case FileInfo::Data::S2DNans: {
                    auto it = static_cast<casacore::Array<casacore::Int64>*>(data)->begin();
                    for (size_t s = 0; s < nstokes; s++) {
                        for (size_t c = 0; c < nchannels; c++) {
                            channelStats[s][c].nanCount = *it++;
                        }
                    }
                    break;
                }
                default: {
                }
            }
            
            delete data;
        }
    }
}

void FileLoader::loadStats2DHist() {
    FileInfo::Data ds = FileInfo::Data::S2DHist;
    
    if (hasData(ds)) {
        const ipos& statDims = getStatsDataShape(ds);
        size_t nbins = statDims[0];

        // We can handle 2D, 3D and 4D in the same way
        if ((ndims == 2 && statDims.isEqual(ipos(1, nbins)))
            || (ndims == 3 && statDims.isEqual(ipos(2, nbins, nchannels)))
            || (ndims == 4 && statDims.isEqual(ipos(3, nbins, nchannels, nstokes)))) {
            auto data = static_cast<casacore::Array<casacore::Int64>*>(getStatsData(ds));
            auto it = data->begin();
        
            for (size_t s = 0; s < nstokes; s++) {
                for (size_t c = 0; c < nchannels; c++) {
                    channelStats[s][c].histogramBins.resize(nbins);
                    for (size_t b = 0; b < nbins; b++) {
                        channelStats[s][c].histogramBins[b] = *it++;
                    }
                }
            }
            
            delete data;
        }
    }
}

// TODO: untested

void FileLoader::loadStats2DPercent() {
    FileInfo::Data dsr = FileInfo::Data::Ranks;
    FileInfo::Data dsp = FileInfo::Data::S2DPercent;
    
    if (hasData(dsp) && hasData(dsr)) {
        const ipos& dimsVals = getStatsDataShape(dsp);
        const ipos& dimsRanks = getStatsDataShape(dsr);

        size_t nranks = dimsRanks[0];
    
        // We can handle 2D, 3D and 4D in the same way
        if ((ndims == 2 && dimsVals.isEqual(ipos(1, nranks)))
            || (ndims == 3 && dimsVals.isEqual(ipos(2, nranks, nchannels)))
            || (ndims == 4 && dimsVals.isEqual(ipos(3, nranks, nchannels, nstokes)))) {
            
            auto ranks = static_cast<casacore::Array<casacore::Float>*>(getStatsData(dsr));
            auto data = static_cast<casacore::Array<casacore::Float>*>(getStatsData(dsp));
        
            auto it = data->begin();
            auto itr = ranks->begin();

            for (size_t s = 0; s < nstokes; s++) {
                for (size_t c = 0; c < nchannels; c++) {
                    channelStats[s][c].percentiles.resize(nranks);
                    channelStats[s][c].percentileRanks.resize(nranks);
                    for (size_t r = 0; r < nranks; r++) {
                        channelStats[s][c].percentiles[r] = *it++;
                        channelStats[s][c].percentileRanks[r] = *itr++;
                    }
                }
            }
            
            delete ranks;
            delete data;
        }
    }
}

void FileLoader::loadStats3DBasic(FileInfo::Data ds) {
    if (hasData(ds)) {
        const ipos& statDims = getStatsDataShape(ds);
                    
        // We can handle 3D and 4D in the same way
        if ((ndims == 3 && statDims.size() == 0)
            || (ndims == 4 && statDims.isEqual(ipos(1, nstokes)))) {
            
            auto data = getStatsData(ds);
            
            switch(ds) {
                case FileInfo::Data::S3DMax: {
                    auto it = static_cast<casacore::Array<casacore::Float>*>(data)->begin();
                    for (size_t s = 0; s < nstokes; s++) {
                        cubeStats[s].maxVal = *it++;
                    }
                    break;
                }
                case FileInfo::Data::S3DMin: {
                    auto it = static_cast<casacore::Array<casacore::Float>*>(data)->begin();
                    for (size_t s = 0; s < nstokes; s++) {
                        cubeStats[s].minVal = *it++;
                    }
                    break;
                }
                case FileInfo::Data::S3DMean: {
                    auto it = static_cast<casacore::Array<casacore::Float>*>(data)->begin();
                    for (size_t s = 0; s < nstokes; s++) {
                        cubeStats[s].mean = *it++;
                    }
                    break;
                }
                case FileInfo::Data::S3DNans: {
                    auto it = static_cast<casacore::Array<casacore::Int64>*>(data)->begin();
                    for (size_t s = 0; s < nstokes; s++) {
                        cubeStats[s].nanCount = *it++;
                    }
                    break;
                }
                default: {
                }
            }
            
            delete data;
        }
    }
}

void FileLoader::loadStats3DHist() {
    FileInfo::Data ds = FileInfo::Data::S3DHist;
    
    if (hasData(ds)) {
        const ipos& statDims = getStatsDataShape(ds);
        size_t nbins = statDims[0];
        
        // We can handle 3D and 4D in the same way
        if ((ndims == 3 && statDims.isEqual(ipos(1, nbins)))
            || (ndims == 4 && statDims.isEqual(ipos(2, nbins, nstokes)))) {
            auto data = static_cast<casacore::Array<casacore::Int64>*>(getStatsData(ds));           
            auto it = data->begin();
            
            for (size_t s = 0; s < nstokes; s++) {
                cubeStats[s].histogramBins.resize(nbins);
                for (size_t b = 0; b < nbins; b++) {
                    cubeStats[s].histogramBins[b] = *it++;
                }
            }
            
            delete data;
        }
    }
}

// TODO: untested

void FileLoader::loadStats3DPercent() {
    FileInfo::Data dsr = FileInfo::Data::Ranks;
    FileInfo::Data dsp = FileInfo::Data::S2DPercent;
    
    if (hasData(dsp) && hasData(dsr)) {
        
        const ipos& dimsVals = getStatsDataShape(dsp);
        const ipos& dimsRanks = getStatsDataShape(dsr);

        size_t nranks = dimsRanks[0];
    
        // We can handle 3D and 4D in the same way
        if ((ndims == 3 && dimsVals.isEqual(ipos(1, nranks)))
            || (ndims == 4 && dimsVals.isEqual(ipos(2, nranks, nstokes)))) {
            
            auto ranks = static_cast<casacore::Array<casacore::Float>*>(getStatsData(dsr));
            auto data = static_cast<casacore::Array<casacore::Float>*>(getStatsData(dsp));
            
            auto it = data->begin();
            auto itr = ranks->begin();

            for (size_t s = 0; s < nstokes; s++) {
                cubeStats[s].percentiles.resize(nranks);
                cubeStats[s].percentileRanks.resize(nranks);
                for (size_t r = 0; r < nranks; r++) {
                    cubeStats[s].percentiles[r] = *it++;
                    cubeStats[s].percentileRanks[r] = *itr++;
                }
            }
            
            delete ranks;
            delete data;
        }
    }
}

void FileLoader::loadImageStats(bool loadPercentiles) {
    channelStats.resize(nstokes);
    for (size_t s = 0; s < nstokes; s++) {
        channelStats[s].resize(nchannels);
    }
    cubeStats.resize(nstokes);
    
    if (hasData(FileInfo::Data::Stats)) {
        if (hasData(FileInfo::Data::Stats2D)) {
            loadStats2DBasic(FileInfo::Data::S2DMax);
            loadStats2DBasic(FileInfo::Data::S2DMin);
            loadStats2DBasic(FileInfo::Data::S2DMean);
            loadStats2DBasic(FileInfo::Data::S2DNans);

            loadStats2DHist();
            
            if (loadPercentiles) {
                loadStats2DPercent();
            }
            
            // If we loaded all the 2D stats successfully, assume all channel stats are valid
            for (size_t s = 0; s < nstokes; s++) {
                for (size_t c = 0; c < nchannels; c++) {
                    channelStats[s][c].valid = true;
                }
            }
        }
        
        if (hasData(FileInfo::Data::Stats3D)) {        
            loadStats3DBasic(FileInfo::Data::S3DMax);       
            loadStats3DBasic(FileInfo::Data::S3DMin);      
            loadStats3DBasic(FileInfo::Data::S3DMean);     
            loadStats3DBasic(FileInfo::Data::S3DNans);

            loadStats3DHist();
            
            if (loadPercentiles) {
                loadStats3DPercent();
            }
            
            // If we loaded all the 3D stats successfully, assume all cube stats are valid
            for (size_t s = 0; s < nstokes; s++) {
                cubeStats[s].valid = true;
            }
        }
    }
}

FileInfo::ImageStats& FileLoader::getImageStats(int stokes, int channel) {
    return (channel >= 0 ? channelStats[stokes][channel] : cubeStats[stokes]);
}

bool FileLoader::getCursorSpectralData(std::vector<float>& data, int stokes, int cursorX, int cursorY) {
    // Must be implemented in subclasses
    return false;
}
