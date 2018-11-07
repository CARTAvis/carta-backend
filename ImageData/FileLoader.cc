#include "CasaLoader.h"
#include "FileLoader.h"
#include "HDF5Loader.h"
#include "FITSLoader.h"
#include "MIRIADLoader.h"

using namespace carta;

FileLoader* FileLoader::getLoader(const std::string &file) {
    casacore::ImageOpener::ImageTypes type = FileInfo::fileType(file);
    switch(type) {
    case casacore::ImageOpener::AIPSPP:
        return new CasaLoader(file);
    case casacore::ImageOpener::FITS:
        return new FITSLoader(file);
        break;
    case casacore::ImageOpener::MIRIAD:
        return new MIRIADLoader(file);
        break;
    case casacore::ImageOpener::GIPSY:
        break;
    case casacore::ImageOpener::CAIPS:
        break;
    case casacore::ImageOpener::NEWSTAR:
        break;
    case casacore::ImageOpener::HDF5:
        return new HDF5Loader(file);
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
