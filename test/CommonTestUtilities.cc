/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <functional>
#include <stdexcept>

#include <casacore/images/Images/FITSImage.h>
#include <casacore/images/Images/PagedImage.h>
#include <casacore/images/Images/SubImage.h>
#include <lattices/Lattices/MaskedLatticeIterator.h>

#include "CommonTestUtilities.h"
#include "Logger/Logger.h"
#include "Util/App.h"

fs::path TestRoot() {
    std::string path_string;
    fs::path root;
    if (FindExecutablePath(path_string)) {
        root = fs::path(path_string).lexically_normal().parent_path();
    } else {
        root = fs::current_path();
    }
    return root;
}

fs::path UserDirectory() {
    return fs::path(getenv("HOME")) / CARTA_USER_FOLDER_PREFIX;
}

std::string ImageGenerator::GeneratedFitsImagePath(const std::string& params, const std::string& opts) {
    fs::path root = TestRoot();

    std::string filename = fmt::format("{:x}.fits", std::hash<std::string>{}(params + "/" + opts));
    fs::path fitspath = (root / "data" / "generated" / filename);
    std::string fitspath_str = fitspath.string();

    if (!fs::exists(fitspath)) {
        std::string generator_path = (root / "bin" / "make_image.py").string();
        std::string fitscmd = fmt::format("{} {} -o {} {}", generator_path, opts, fitspath_str, params);
        auto result = system(fitscmd.c_str());
    }

    return fitspath_str;
}

std::string ImageGenerator::GeneratedHdf5ImagePath(const std::string& params, const std::string& opts) {
    fs::path root = TestRoot();

    std::string fitspath_str = GeneratedFitsImagePath(params, opts);
    std::string hdf5path_str = fmt::format("{}.hdf5", fitspath_str);
    fs::path hdf5path = fs::path(hdf5path_str);

    if (!fs::exists(hdf5path)) {
        std::string hdf5cmd = fmt::format("fits2idia -o {} {}", hdf5path_str, fitspath_str);
        auto result = system(hdf5cmd.c_str());
    }

    return hdf5path_str;
}

std::string FileFinder::DataPath(const std::string& filename) {
    return (TestRoot() / "data" / filename).string();
}

std::string FileFinder::FitsImagePath(const std::string& filename) {
    return (TestRoot() / "data" / "images" / "fits" / filename).string();
}

std::string FileFinder::CasaImagePath(const std::string& filename) {
    return (TestRoot() / "data" / "images" / "casa" / filename).string();
}

std::string FileFinder::Hdf5ImagePath(const std::string& filename) {
    return (TestRoot() / "data" / "images" / "hdf5" / filename).string();
}

std::string FileFinder::FitsTablePath(const std::string& filename) {
    return (TestRoot() / "data" / "tables" / "fits" / filename).string();
}

std::string FileFinder::XmlTablePath(const std::string& filename) {
    return (TestRoot() / "data" / "tables" / "xml" / filename).string();
}

float DataReader::ReadPointXY(hsize_t x, hsize_t y, hsize_t channel, hsize_t stokes) {
    return ReadRegion({x, y, channel, stokes}, {x + 1, y + 1, channel + 1, stokes + 1})[0];
}

std::vector<float> DataReader::ReadProfileX(hsize_t y, hsize_t channel, hsize_t stokes) {
    return ReadRegion({0, y, channel, stokes}, {_width, y + 1, channel + 1, stokes + 1});
}

std::vector<float> DataReader::ReadProfileY(hsize_t x, hsize_t channel, hsize_t stokes) {
    return ReadRegion({x, 0, channel, stokes}, {x + 1, _height, channel + 1, stokes + 1});
}

std::vector<float> DataReader::ReadXY(hsize_t channel, hsize_t stokes) {
    return ReadRegion({0, 0, channel, stokes}, {_width, _height, channel + 1, stokes + 1});
}

hsize_t DataReader::Width() {
    return _width;
}

hsize_t DataReader::Height() {
    return _height;
}

FitsDataReader::FitsDataReader(const std::string& imgpath) {
    int status(0);

    fits_open_file(&_imgfile, imgpath.c_str(), READONLY, &status);

    if (status != 0) {
        throw std::runtime_error(fmt::format("Could not open FITS file. Error status: {}", status));
    }

    int bitpix;
    fits_get_img_type(_imgfile, &bitpix, &status);

    if (status != 0) {
        throw std::runtime_error(fmt::format("Could not read image type. Error status: {}", status));
    }

    if (bitpix != -32) {
        throw std::runtime_error("Currently only supports FP32 files");
    }

    fits_get_img_dim(_imgfile, &_N, &status);

    if (status != 0) {
        throw std::runtime_error(fmt::format("Could not read image dimensions. Error status: {}", status));
    }

    if (_N < 2 || _N > 4) {
        throw std::runtime_error("Currently only supports 2D, 3D and 4D cubes");
    }

    std::vector<long> dims(_N);

    fits_get_img_size(_imgfile, _N, dims.data(), &status);

    if (status != 0) {
        throw std::runtime_error(fmt::format("Could not read image size. Error status: {}", status));
    }

    for (auto& d : dims) {
        _dims.push_back(d);
    }
    _stokes = _N == 4 ? _dims[3] : 1;
    _depth = _N >= 3 ? _dims[2] : 1;
    _height = _dims[1];
    _width = _dims[0];
}

FitsDataReader::~FitsDataReader() {
    int status(0);

    if (_imgfile) {
        fits_close_file(_imgfile, &status);
    }
}

std::vector<float> FitsDataReader::ReadRegion(std::vector<hsize_t> start, std::vector<hsize_t> end) {
    int status(0);
    std::vector<float> result;

    long fpixel[_N];
    long lpixel[_N];
    long inc[_N];
    long result_size = 1;

    for (int d = 0; d < _N; d++) {
        // Truncate or extend the first and last pixel array to the image dimensions
        // ...and convert from 0-indexing to 1-indexing
        // ...and convert end vector from exclusive to inclusive
        fpixel[d] = d < start.size() ? start[d] + 1 : 1;
        lpixel[d] = d < end.size() ? end[d] : 1;
        // Set the increment to 1
        inc[d] = 1;

        // Calculate the expected result size
        result_size *= lpixel[d] - fpixel[d] + 1;
    }

    result.resize(result_size);

    fits_read_subset(_imgfile, TFLOAT, fpixel, lpixel, inc, nullptr, result.data(), nullptr, &status);

    if (status != 0) {
        throw std::runtime_error(fmt::format("Could not read image data. Error status: {}", status));
    }

    return result;
}

Hdf5DataReader::Hdf5DataReader(const std::string& imgpath) {
    _imgfile = H5::H5File(imgpath, H5F_ACC_RDONLY);
    _group = _imgfile.openGroup("0");
    _dataset = _group.openDataSet("DATA");

    auto data_space = _dataset.getSpace();
    _N = data_space.getSimpleExtentNdims();
    _dims.resize(_N);
    data_space.getSimpleExtentDims(_dims.data(), nullptr);

    std::reverse(_dims.begin(), _dims.end());
    _stokes = _N == 4 ? _dims[3] : 1;
    _depth = _N >= 3 ? _dims[2] : 1;
    _height = _dims[1];
    _width = _dims[0];
}

std::vector<float> Hdf5DataReader::ReadRegion(std::vector<hsize_t> start, std::vector<hsize_t> end) {
    std::vector<float> result;
    std::vector<hsize_t> h5_start;
    std::vector<hsize_t> h5_count;
    hsize_t result_size = 1;

    for (int d = 0; d < _N; d++) {
        // Calculate the expected result size
        h5_start.insert(h5_start.begin(), d < start.size() ? start[d] : 0);
        h5_count.insert(h5_count.begin(), d < start.size() ? end[d] - start[d] : 1);
        result_size *= end[d] - start[d];
    }

    result.resize(result_size);
    H5::DataSpace mem_space(1, &result_size);

    auto file_space = _dataset.getSpace();
    file_space.selectHyperslab(H5S_SELECT_SET, h5_count.data(), h5_start.data());
    _dataset.read(result.data(), H5::PredType::NATIVE_FLOAT, mem_space, file_space);

    return result;
}

hid_t Hdf5DataReader::GroupId() {
    return _group.getId();
}

CartaEnvironment::~CartaEnvironment() {}

void CartaEnvironment::SetUp() {
    // create directory for generated images
    fs::create_directory(TestRoot() / "data" / "generated");
}

void CartaEnvironment::TearDown() {
    // delete directory of generated images
    fs::remove_all(TestRoot() / "data" / "generated");
}

bool OpenImage(std::shared_ptr<casacore::ImageInterface<float>>& image, const std::string& filename, uInt hdu_num) {
    bool image_ok(false);
    try {
        casacore::ImageOpener::ImageTypes image_types = casacore::ImageOpener::imageType(filename);
        switch (image_types) {
            case casacore::ImageOpener::AIPSPP:
                image = std::make_shared<casacore::PagedImage<float>>(filename);
                image_ok = true;
                break;
            case casacore::ImageOpener::FITS:
                image = std::make_shared<casacore::FITSImage>(filename, 0, hdu_num);
                image_ok = true;
                break;
            default:
                spdlog::error("Unsupported file type: {}", image_types);
                break;
        }
    } catch (const casacore::AipsError& x) {
        spdlog::error("Error on opening the file: {}", x.getMesg());
    }
    return image_ok;
}

void GetImageData(std::vector<float>& data, std::shared_ptr<const casacore::ImageInterface<float>> image, int stokes, AxisRange z_range,
    AxisRange x_range, AxisRange y_range) {
    // Get spectral and stokes indices
    casacore::CoordinateSystem coord_sys = image->coordinates();
    int spectral_axis = coord_sys.spectralAxisNumber();
    if (spectral_axis < 0 && image->ndim() > 2) {
        spectral_axis = 2; // assume spectral axis
    }
    int stokes_axis = coord_sys.polarizationAxisNumber();
    if (stokes_axis < 0 && image->ndim() > 3) {
        stokes_axis = 3; // assume stokes axis
    }

    // Get a slicer
    casacore::IPosition start(image->shape().size());
    start = 0;
    casacore::IPosition end(image->shape());
    end -= 1;

    auto x_axis_size = image->shape()[0];
    auto y_axis_size = image->shape()[1];

    // Set x range
    if ((x_range.from >= 0) && (x_range.from < x_axis_size) && (x_range.to >= 0) && (x_range.to < x_axis_size) &&
        (x_range.from <= x_range.to)) {
        start(0) = x_range.from;
        end(0) = x_range.to;
    } else {
        start(0) = 0;
        end(0) = x_axis_size - 1;
    }

    // Set y range
    if ((y_range.from >= 0) && (y_range.from < y_axis_size) && (y_range.to >= 0) && (y_range.to < y_axis_size) &&
        (y_range.from <= y_range.to)) {
        start(1) = y_range.from;
        end(1) = y_range.to;
    } else {
        start(1) = 0;
        end(1) = y_axis_size - 1;
    }

    // Set z range
    if (spectral_axis >= 0) {
        auto z_axis_size = image->shape()[spectral_axis];
        if ((z_range.from >= 0) && (z_range.from < z_axis_size) && (z_range.to >= 0) && (z_range.to < z_axis_size) &&
            (z_range.from <= z_range.to)) {
            start(spectral_axis) = z_range.from;
            end(spectral_axis) = z_range.to;
        } else {
            start(spectral_axis) = 0;
            end(spectral_axis) = z_axis_size - 1;
        }
    }

    // Set stokes range
    if (stokes_axis >= 0) {
        auto stokes_axis_size = image->shape()[stokes_axis];
        if ((stokes >= 0) && (stokes < stokes_axis_size)) {
            start(stokes_axis) = stokes;
            end(stokes_axis) = stokes;
        } else {
            spdlog::error("Invalid stokes: {}", stokes);
            return;
        }
    }

    // Get image data
    casacore::Slicer section(start, end, casacore::Slicer::endIsLast);
    data.resize(section.length().product());
    casacore::Array<float> tmp(section.length(), data.data(), casacore::StorageInitPolicy::SHARE);
    casacore::SubImage<float> sub_image(*image, section);
    casacore::RO_MaskedLatticeIterator<float> lattice_iter(sub_image);

    for (lattice_iter.reset(); !lattice_iter.atEnd(); ++lattice_iter) {
        casacore::Array<float> cursor_data = lattice_iter.cursor();
        if (image->isMasked()) {
            casacore::Array<float> masked_data(cursor_data);
            const casacore::Array<bool> cursor_mask = lattice_iter.getMask();
            bool del_mask_ptr;
            const bool* cursor_mask_ptr = cursor_mask.getStorage(del_mask_ptr);
            bool del_data_ptr;
            float* masked_data_ptr = masked_data.getStorage(del_data_ptr);
            for (size_t i = 0; i < cursor_data.nelements(); ++i) {
                if (!cursor_mask_ptr[i]) {
                    masked_data_ptr[i] = NAN;
                }
            }
            cursor_mask.freeStorage(cursor_mask_ptr, del_mask_ptr);
            masked_data.putStorage(masked_data_ptr, del_data_ptr);
        }
        casacore::IPosition cursor_shape(lattice_iter.cursorShape());
        casacore::IPosition cursor_position(lattice_iter.position());
        casacore::Slicer cursor_slicer(cursor_position, cursor_shape);
        tmp(cursor_slicer) = cursor_data;
    }
}

std::vector<float> GetSpatialProfileValues(const CARTA::SpatialProfile& profile) {
    std::string buffer = profile.raw_values_fp32();
    std::vector<float> values(buffer.size() / sizeof(float));
    memcpy(values.data(), buffer.data(), buffer.size());
    return values;
}

void CmpSpatialProfiles(
    const std::vector<CARTA::SpatialProfileData>& data_vec, const std::pair<std::vector<float>, std::vector<float>>& data_profiles) {
    EXPECT_EQ(data_vec.size(), 1);
    for (const auto& data : data_vec) {
        CmpVectors<float>(GetSpatialProfileValues(data.profiles(0)), data_profiles.first);
        CmpVectors<float>(GetSpatialProfileValues(data.profiles(1)), data_profiles.second);
    }
}

void CmpVectors(const std::vector<float>& data1, const std::vector<float>& data2, float abs_err) {
    EXPECT_EQ(data1.size(), data2.size());
    if (data1.size() == data2.size()) {
        for (int i = 0; i < data1.size(); ++i) {
            CmpValues(data1[i], data2[i], abs_err);
        }
    }
}

void CmpValues(float data1, float data2, float abs_err) {
    if (!std::isnan(data1) || !std::isnan(data2)) {
        if (abs_err > 0) {
            EXPECT_NEAR(data1, data2, abs_err);
        } else {
            EXPECT_FLOAT_EQ(data1, data2);
        }
    }
}

bool CmpHistograms(const carta::Histogram& hist1, const carta::Histogram& hist2) {
    if (hist1.GetNbins() != hist2.GetNbins()) {
        return false;
    }
    if (fabs(hist1.GetMinVal() - hist2.GetMinVal()) > std::numeric_limits<float>::epsilon()) {
        return false;
    }
    if (fabs(hist1.GetMaxVal() - hist2.GetMaxVal()) > std::numeric_limits<float>::epsilon()) {
        return false;
    }

    for (auto i = 0; i < hist1.GetNbins(); i++) {
        auto bin_a = hist1.GetHistogramBins()[i];
        auto bin_b = hist2.GetHistogramBins()[i];
        if (bin_a != bin_b) {
            return false;
        }
    }

    return true;
}
