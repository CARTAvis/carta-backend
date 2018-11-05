#pragma once

#include "FileLoader.h"
#include <casacore/lattices/Lattices/HDF5Lattice.h>
#include <H5Cpp.h>
#include <H5File.h>
#include <string>
#include <unordered_map>

namespace carta {

class HDF5Loader : public FileLoader {
public:
    HDF5Loader(const std::string &filename);
    void openFile(const std::string &file, const std::string &hdu) override;
    bool hasData(FileInfo::Data ds) const override;
    image_ref loadData(FileInfo::Data ds) override;
    int stokesAxis() override;

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
    std::string parent = "main";
    auto it = dataSets.find(parent);
    if(it == dataSets.end()) return false;

    std::string data;
    switch(ds) {
    case FileInfo::Data::XY:
        return it->second.shape().size() >= 2;
    case FileInfo::Data::XYZ:
        return it->second.shape().size() >= 3;
    case FileInfo::Data::XYZW:
        return it->second.shape().size() >= 4;
    case FileInfo::Data::YX:
    case FileInfo::Data::ZYX:
    case FileInfo::Data::ZYXW:
        data = dataSetToString(ds);
        break;
    default:
        return false;
    }
    auto group_ptr = it->second.group();
    return casacore::HDF5Group::exists(*group_ptr, data);
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
        { FileInfo::Data::Ranks,      "PERCENTILE_RANKS" },
    };
    return (um.find(ds) != um.end()) ? um[ds] : "";
}

int HDF5Loader::stokesAxis() {
    // returns -1 if no stokes axis
    H5::H5File hdf5file(file, H5F_ACC_RDONLY);
    H5::Group topLevelGroup = hdf5file.openGroup(hdf5Hdu);
    hid_t groupId = topLevelGroup.getId();
    char* type;
    // read CTYPE3
    hid_t ctypeID = H5Aopen_name(groupId, "CTYPE3");
    if (ctypeID > 0) {
        H5Aread(ctypeID, H5T_STRING, type);
        H5Aclose(ctypeID);
        std::string typestr3(type);
        if (typestr3.compare("STOKES")) return 2;
        // read CTYPE4
        ctypeID = H5Aopen_name(groupId, "CTYPE4");
	if (ctypeID > 0) {
            H5Aread(ctypeID, H5T_STRING, type);
            std::string typestr4(type);
            H5Aclose(ctypeID);
            if (typestr4.compare("STOKES")) return 3;
        }
    }
    // no stokes axis
    return -1;
}

} // namespace carta
