#pragma once

#include "FileLoader.h"
#include "HDF5Attributes.h"

#include <casacore/lattices/Lattices/HDF5Lattice.h>
#include <unordered_map>

namespace carta {

class HDF5Loader : public FileLoader {
public:
    HDF5Loader(const std::string &filename);
    void openFile(const std::string &file, const std::string &hdu) override;
    bool hasData(FileInfo::Data ds) const override;
    image_ref loadData(FileInfo::Data ds) override;
    bool getPixelMaskSlice(casacore::Array<bool>& mask, const casacore::Slicer& slicer) override;
    const casacore::CoordinateSystem& getCoordSystem() override;
    void findCoords(int& spectralAxis, int& stokesAxis) override;
    bool getCursorSpectralData(std::vector<float>& data, int stokes, int cursorX, int cursorY) override;
    
private:
    std::string file, hdf5Hdu;
    casacore::HDF5Lattice<float> image;
    std::unique_ptr<casacore::HDF5Lattice<float>> swizzledImage;
    
    std::string dataSetToString(FileInfo::Data ds) const;
    
    template <typename T> const ipos getStatsDataShapeTyped(FileInfo::Data ds);
    template <typename S, typename D> casacore::ArrayBase* getStatsDataTyped(FileInfo::Data ds);
    
    const ipos getStatsDataShape(FileInfo::Data ds) override;
    casacore::ArrayBase* getStatsData(FileInfo::Data ds) override;
};

HDF5Loader::HDF5Loader(const std::string &filename)
    : file(filename),
      hdf5Hdu("0")
{}

void HDF5Loader::openFile(const std::string &filename, const std::string &hdu) {
    image = casacore::HDF5Lattice<float>(filename, dataSetToString(FileInfo::Data::Image), hdu);
    
    // We need this immediately because dataSetToString uses it to find the name of the swizzled dataset
    ndims = image.shape().size();
    
    if (hasData(FileInfo::Data::Swizzled)) {
        swizzledImage = std::unique_ptr<casacore::HDF5Lattice<float>>(new casacore::HDF5Lattice<float>(filename, dataSetToString(FileInfo::Data::Swizzled), hdu));
    }
    
}

// We assume that the main image dataset is always loaded and therefore available.
// For everything else, we refer back to the file.
bool HDF5Loader::hasData(FileInfo::Data ds) const {
    switch(ds) {
        case FileInfo::Data::Image:
            return true;
        case FileInfo::Data::XY:
            return ndims >= 2;
        case FileInfo::Data::XYZ:
            return ndims >= 3;
        case FileInfo::Data::XYZW:
            return ndims >= 4;
        default:
            auto group_ptr = image.group();
            std::string data(dataSetToString(ds));
            if (data.empty()) {
                return false;
            }            
            return casacore::HDF5Group::exists(*group_ptr, data);
    }
}

// TODO: when we fix the typing issue, this should probably return any dataset again, for consistency.
typename HDF5Loader::image_ref HDF5Loader::loadData(FileInfo::Data ds) {
    switch(ds) {
        case FileInfo::Data::Image:
            return image;
        case FileInfo::Data::XY:
            if (ndims == 2) {
                return image;
            }
            break;
        case FileInfo::Data::XYZ:
            if (ndims == 3) {
                return image;
            }
            break;
        case FileInfo::Data::XYZW:
            if (ndims == 4) {
                return image;
            }
            break;
        case FileInfo::Data::Swizzled:
            if (swizzledImage.get()) {
                return *swizzledImage;
            }
            break;
        case FileInfo::Data::ZYX:
            if (ndims == 3 && swizzledImage.get()) {
                return *swizzledImage;
            }
            break;
        case FileInfo::Data::ZYXW:
            if (ndims == 4 && swizzledImage.get()) {
                return *swizzledImage;
            }
            break;
        default:
            break;
    }
        
    throw casacore::HDF5Error("Unable to load dataset " + dataSetToString(ds) + ".");
}

bool HDF5Loader::getPixelMaskSlice(casacore::Array<bool>& mask, const casacore::Slicer& slicer) {
    // HDF5Lattice is not a masked lattice
    return false;
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

std::string HDF5Loader::dataSetToString(FileInfo::Data ds) const {
    static std::unordered_map<FileInfo::Data, std::string, EnumClassHash> um = {
        { FileInfo::Data::Image,      "DATA" },
        { FileInfo::Data::YX,         "SwizzledData/YX" },
        { FileInfo::Data::ZYX,        "SwizzledData/ZYX" },
        { FileInfo::Data::ZYXW,       "SwizzledData/ZYXW" },
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
    
    switch(ds) {
        case FileInfo::Data::XY:
            return ndims == 2 ? dataSetToString(FileInfo::Data::Image) : "";
        case FileInfo::Data::XYZ:
            return ndims == 3 ? dataSetToString(FileInfo::Data::Image) : "";
        case FileInfo::Data::XYZW:
            return ndims == 4 ? dataSetToString(FileInfo::Data::Image) : "";
        case FileInfo::Data::Swizzled:
            switch(ndims) {
                case 2:
                    return dataSetToString(FileInfo::Data::YX);
                case 3:
                    return dataSetToString(FileInfo::Data::ZYX);
                case 4:
                    return dataSetToString(FileInfo::Data::ZYXW);
                default:
                    return "";
            }
            break;
        default:
            return (um.find(ds) != um.end()) ? um[ds] : "";
    }
}

const casacore::CoordinateSystem& HDF5Loader::getCoordSystem() {
    // this does not work: 
    // (/casacore/lattices/LEL/LELCoordinates.cc : 69) Failed AlwaysAssert !coords_p.null()
    const casacore::LELImageCoord* lelImCoords =
        dynamic_cast<const casacore::LELImageCoord*>(&(loadData(FileInfo::Data::Image).lelCoordinates().coordinates()));
    return lelImCoords->coordinates();
}

// TODO: The datatype used to create the HDF5DataSet has to match the native type exactly, but the data can be read into an array of the same type class. We cannot guarantee a particular native type -- e.g. some files use doubles instead of floats. This necessitates this complicated templating, at least for now.
const HDF5Loader::ipos HDF5Loader::getStatsDataShape(FileInfo::Data ds) {
    auto dtype = casacore::HDF5DataSet::getDataType((*image.group()).getHid(), dataSetToString(ds));
    
    switch(dtype) {
        case casacore::TpInt:
        {
            return getStatsDataShapeTyped<casacore::Int>(ds);
        }
        case casacore::TpInt64:
        {
            return getStatsDataShapeTyped<casacore::Int64>(ds);
        }
        case casacore::TpFloat:
        {
            return getStatsDataShapeTyped<casacore::Float>(ds);
        }
        case casacore::TpDouble:
        {
            return getStatsDataShapeTyped<casacore::Double>(ds);
        }
        default: {
            throw casacore::HDF5Error("Dataset " + dataSetToString(ds) + " has an unsupported datatype.");
        }
    }
}

template <typename T>
const HDF5Loader::ipos HDF5Loader::getStatsDataShapeTyped(FileInfo::Data ds) {
    casacore::HDF5DataSet dataSet(*image.group(), dataSetToString(ds), (const T*)0);
    return dataSet.shape();
}

// TODO: The datatype used to create the HDF5DataSet has to match the native type exactly, but the data can be read into an array of the same type class. We cannot guarantee a particular native type -- e.g. some files use doubles instead of floats. This necessitates this complicated templating, at least for now.
casacore::ArrayBase* HDF5Loader::getStatsData(FileInfo::Data ds) {
    auto dtype = casacore::HDF5DataSet::getDataType((*image.group()).getHid(), dataSetToString(ds));
        
    switch(dtype) {
        case casacore::TpInt:
        {
            return getStatsDataTyped<casacore::Int, casacore::Int64>(ds);
        }
        case casacore::TpInt64:
        {
            return getStatsDataTyped<casacore::Int64, casacore::Int64>(ds);
        }
        case casacore::TpFloat:
        {
            return getStatsDataTyped<casacore::Float, casacore::Float>(ds);
        }
        case casacore::TpDouble:
        {
            return getStatsDataTyped<casacore::Double, casacore::Float>(ds);
        }
        default: {
            throw casacore::HDF5Error("Dataset " + dataSetToString(ds) + " has an unsupported datatype.");
        }
    }
}

// TODO: We need to use the C API to read scalar datasets for now, but we should patch casacore to handle them correctly.
template <typename S, typename D>
casacore::ArrayBase* HDF5Loader::getStatsDataTyped(FileInfo::Data ds) {
    casacore::HDF5DataSet dataSet(*image.group(), dataSetToString(ds), (const S*)0);

    if (dataSet.shape().size() == 0) {
        // Scalar dataset hackaround
        D value;
        casacore::HDF5DataType dtype((D*)0);
        H5Dread(dataSet.getHid(), dtype.getHidMem(), H5S_ALL, H5S_ALL, H5P_DEFAULT, &value);
        casacore::ArrayBase* scalar = new casacore::Array<D>(ipos(1, 1), value);
        return scalar;
    }
    
    casacore::ArrayBase* data = new casacore::Array<D>();
    dataSet.get(casacore::Slicer(ipos(dataSet.shape().size(), 0), dataSet.shape()), *data);
    return data;
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
bool HDF5Loader::getCursorSpectralData(std::vector<float>& data, int stokes, int cursorX, int cursorY) {
    bool dataOK(false);
    if (hasData(FileInfo::Data::Swizzled)) {
        casacore::Slicer slicer;
        if (ndims == 4) {
            slicer = casacore::Slicer(ipos(4, 0, cursorY, cursorX, stokes), ipos(4, nchannels, 1, 1, 1));
        } else if (ndims == 3) {
            slicer = casacore::Slicer(ipos(3, 0, cursorY, cursorX), ipos(3, nchannels, 1, 1));
        }

        data.resize(nchannels);
        casacore::Array<float> tmp(slicer.length(), data.data(), casacore::StorageInitPolicy::SHARE);
        try {
            loadData(FileInfo::Data::Swizzled).doGetSlice(tmp, slicer);
            dataOK = true;
        } catch (casacore::AipsError& err) {
            std::cerr << "AIPS ERROR: " << err.getMesg() << std::endl;
        }
    }
    
    return dataOK;
}



} // namespace carta
