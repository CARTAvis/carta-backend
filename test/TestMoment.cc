/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>

#include "Logger/Logger.h"
#include "Moment/ImageMoments.h"
#include "Util.h"

#include <casacore/images/Images/PagedImage.h>
#include <imageanalysis/ImageAnalysis/ImageMoments.h>

#ifdef _BOOST_FILESYSTEM_
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

using namespace std;
using namespace carta;

class MomentTest : public ::testing::Test {
public:
    static string ImagePath(const string& filename) {
        string path_string;
        fs::path path;
        if (FindExecutablePath(path_string)) {
            path = fs::path(path_string).parent_path();
        } else {
            path = fs::current_path();
        }
        return (path / "data/images/casa" / filename).string();
    }

    static bool Exists(const fs::path& p, fs::file_status s = fs::file_status{}) {
        if (fs::status_known(s) ? fs::exists(s) : fs::exists(p)) {
            return true;
        }
        return false;
    }

    static void GetImageData(std::shared_ptr<const casacore::ImageInterface<casacore::Float>> image, std::vector<float>& data) {
        // Get spectral and stokes indices
        casacore::CoordinateSystem coord_sys = image->coordinates();
        casacore::Vector<casacore::Int> linear_axes = coord_sys.linearAxesNumbers();
        int spectral_axis = coord_sys.spectralAxisNumber();
        int stokes_axis = coord_sys.polarizationAxisNumber();

        // Get a slicer
        casacore::IPosition start(image->shape().size());
        start = 0;
        casacore::IPosition end(image->shape());
        end -= 1;

        if (spectral_axis >= 0) {
            start(spectral_axis) = 0;
            end(spectral_axis) = image->shape()[spectral_axis] - 1;
        }
        if (stokes_axis >= 0) {
            start(stokes_axis) = 0;
            end(stokes_axis) = 0;
        }

        // Get image data
        casacore::Slicer section(start, end, casacore::Slicer::endIsLast);
        data.resize(section.length().product());
        casacore::Array<float> tmp(section.length(), data.data(), casacore::StorageInitPolicy::SHARE);
        casacore::SubImage<float> subimage(*image, section);
        casacore::RO_MaskedLatticeIterator<float> lattice_iter(subimage);

        for (lattice_iter.reset(); !lattice_iter.atEnd(); ++lattice_iter) {
            casacore::Array<float> cursor_data = lattice_iter.cursor();

            if (image->isMasked()) {
                casacore::Array<float> masked_data(cursor_data); // reference the same storage
                const casacore::Array<bool> cursor_mask = lattice_iter.getMask();

                // Apply cursor mask to cursor data: set masked values to std::numeric_limits<float>::min(), booleans are used to delete
                // copy of data if necessary
                bool del_mask_ptr;
                const bool* cursor_mask_ptr = cursor_mask.getStorage(del_mask_ptr);
                bool del_data_ptr;
                float* masked_data_ptr = masked_data.getStorage(del_data_ptr);
                for (size_t i = 0; i < cursor_data.nelements(); ++i) {
                    if (!cursor_mask_ptr[i]) {
                        masked_data_ptr[i] = std::numeric_limits<float>::min();
                    }
                }

                // free storage for cursor arrays
                cursor_mask.freeStorage(cursor_mask_ptr, del_mask_ptr);
                masked_data.putStorage(masked_data_ptr, del_data_ptr);
            }

            casacore::IPosition cursor_shape(lattice_iter.cursorShape());
            casacore::IPosition cursor_position(lattice_iter.position());
            casacore::Slicer cursor_slicer(cursor_position, cursor_shape); // where to put the data
            tmp(cursor_slicer) = cursor_data;
        }
    }

    static void CompareImageData(std::shared_ptr<const casacore::ImageInterface<casacore::Float>> image1,
        std::shared_ptr<const casacore::ImageInterface<casacore::Float>> image2) {
        std::vector<float> data1;
        std::vector<float> data2;
        GetImageData(image1, data1);
        GetImageData(image2, data2);
        if (data1.size() == data2.size()) {
            for (int i = 0; i < data1.size(); ++i) {
                EXPECT_FLOAT_EQ(data1[i], data2[i]);
            }
        }
    }
};

TEST_F(MomentTest, CheckConsistency) {
    string file_name = ImagePath("M17_SWex.image");

    if (Exists(file_name)) {
        // open an image file
        auto image = std::make_unique<casacore::PagedImage<float>>(file_name);

        // create casa/carta moments generators
        casacore::LogOrigin casa_log("casa::ImageMoment", "createMoments", WHERE);
        casacore::LogIO casa_os(casa_log);
        casacore::LogOrigin carta_log("carta::ImageMoment", "createMoments", WHERE);
        casacore::LogIO carta_os(carta_log);
        casa::ImageMoments<float> casa_image_moments(*image, casa_os, true);
        carta::ImageMoments<float> carta_image_moments(*image, carta_os, true);

        // set moment types
        casacore::Vector<casacore::Int> moments(12);
        moments[0] = 1;
        moments[1] = 0;
        moments[2] = 2;
        moments[3] = 3;
        moments[4] = 4;
        moments[5] = 6;
        moments[6] = 7;
        moments[7] = 8;
        moments[8] = 9;
        moments[9] = 10;
        moments[10] = 11;
        moments[11] = 12;

        // set spectral or stokes axis
        int axis = 2;

        // the other settings
        casacore::Vector<float> include_pix;
        casacore::Vector<float> exclude_pix;
        casacore::Bool do_temp = true;
        casacore::Bool remove_axis = false;

        // calculate moments with casa moment generator
        casa_image_moments.setMoments(moments);
        casa_image_moments.setMomentAxis(axis);
        casa_image_moments.setInExCludeRange(include_pix, exclude_pix);
        auto casa_results = casa_image_moments.createMoments(do_temp, "casa_image_moments", remove_axis);

        // calculate moments with carta moment generator
        carta_image_moments.setMoments(moments);
        carta_image_moments.setMomentAxis(axis);
        carta_image_moments.setInExCludeRange(include_pix, exclude_pix);
        auto carta_results = carta_image_moments.createMoments(do_temp, "carta_image_moments", remove_axis);

        // check the consistency of casa/carta results
        EXPECT_EQ(casa_results.size(), carta_results.size());
        EXPECT_EQ(carta_results.size(), moments.size());

        for (int i = 0; i < casa_results.size(); ++i) {
            auto casa_moment_image = dynamic_pointer_cast<casacore::ImageInterface<casacore::Float>>(casa_results[i]);
            auto carta_moment_image = dynamic_pointer_cast<casacore::ImageInterface<casacore::Float>>(carta_results[i]);

            EXPECT_EQ(casa_moment_image->shape().size(), carta_moment_image->shape().size());
            CompareImageData(casa_moment_image, carta_moment_image);
        }
    } else {
        std::size_t found = file_name.find_last_of("/");
        std::string file_base_name = file_name.substr(found + 1);
        spdlog::warn("File {} does not exists! Ignore the Moment test: CheckConsistency.", file_base_name);
    }
}
