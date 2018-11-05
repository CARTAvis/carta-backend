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

