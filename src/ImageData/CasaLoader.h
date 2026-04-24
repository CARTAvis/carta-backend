/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_IMAGEDATA_CASALOADER_H_
#define CARTA_SRC_IMAGEDATA_CASALOADER_H_

#include <casacore/casa/IO/LockFile.h>
#include <casacore/images/Images/ImageOpener.h>
#include <casacore/images/Images/PagedImage.h>
#include <casacore/images/Images/TempImage.h>

// need to discover the file type:
#include <casacore/casa/Containers/Record.h>
#include <casacore/casa/OS/Directory.h>
#include <casacore/casa/OS/File.h>
#include <casacore/tables/Tables/Table.h>

// to enable ADIOS support in CMakeLists.txt
#ifdef USE_ADIOS
#include "ADIOSImage.h"
#endif
#include "FileLoader.h"

namespace carta {

class CasaLoader : public FileLoader {
public:
    CasaLoader(const std::string& filename);
    bool isAdios2Format(const casacore::String& path);
    bool isAdios2FormatSafe(const casacore::String& path);

private:
    void AllocateImage(const std::string& hdu) override;
    casacore::TempImage<float>* ConvertImageToFloat(casacore::LatticeBase* lattice);
};

CasaLoader::CasaLoader(const std::string& filename) : FileLoader(filename) {}

#ifdef USE_ADIOS
bool CasaLoader::isAdios2FormatSafe(const casacore::String& path) {
    // 1. Basic check: Is it even a directory?
    casacore::Directory dir(path);
    if (!dir.exists())
        return false;

    // 2. Look for the "table.info" file which is plain text
    casacore::File tableInfo(path + "/table.f0.bp/md.idx");
    if (tableInfo.exists()) {
        return true;
        // We can check the type without "opening" the Table formally
        // Search for the string "Adios2StMan" inside table.info
        // (This is much safer as it doesn't trigger the ADIOS2 C++ Engine)
        // ... implementation of a simple string search ...
    }

    return false;
}

bool CasaLoader::isAdios2Format(const casacore::String& path) {
    try {
        // Use Table::Old to ensure read-only
        // Use TableLock::NoLocking to avoid interfering with the ADIOS2 engine
        casacore::Table tab(path, casacore::TableLock(casacore::TableLock::NoLocking), casacore::Table::Old);
        // Look at the data manager info for the first column (usually 'map')
        casacore::Record dminfo = tab.dataManagerInfo();

        for (int i = 0; i < dminfo.nfields(); ++i) {
            casacore::Record sub = dminfo.subRecord(i);
            if (sub.isDefined("TYPE") && sub.asString("TYPE") == "Adios2StMan") {
                printf("It is ADIOS2 file!\n");
                fflush(stdout);
                return true;
            }
        }
        //        tab.close();
    } catch (...) {
        return false;
    }
    return false;
}
#endif

void CasaLoader::AllocateImage(const std::string& /*hdu*/) {
    if (!_image) {
        // Check if image is locked; PagedImage AutoNoReadLocking option tries to acquire a lock and blocks until it is free
        bool create(false), add_request(false), must_exist(false);
        auto lock_file =
            std::unique_ptr<casacore::LockFile>(new casacore::LockFile(_filename + "/table.lock", 5, create, add_request, must_exist));

        if (!lock_file->canLock(casacore::FileLocker::Read)) {
            throw(casacore::AipsError("Cannot open image, locked by another process."));
        }

        lock_file.reset(nullptr);

        bool converted(false);

        try {
#ifdef USE_ADIOS
            bool isADIOS = isAdios2Format(_filename);
            // this logic should really be at a lower level in some CASA function that can read both formats:
            std::cout << "DEBUG : filename " << _filename << " isAdios = " << isADIOS << std::endl;
            if (isADIOS) {
                _image.reset(new ADIOSImage<float>(_filename));
                /* test code to show values:
                casacore::IPosition imageShape = _image->shape();
                int ndim = imageShape.nelements();

                // Create BLC and TRC with the correct number of dimensions
                casacore::IPosition blc(ndim, 0);
                casacore::IPosition trc(ndim, 0); // 0 ok
                const casacore::Slicer slicer(blc, trc, casacore::Slicer::endIsLast);
                casacore::Array<float> tempSlice = _image->getSlice(casacore::Slicer(blc, trc, casacore::Slicer::endIsLast));

                std::cout << "DEBUG : ndim = " << ndim << "tempSlice.ndim() = " << tempSlice.ndim() << std::endl;

                std::cout << "Value at origin: " << tempSlice(casacore::IPosition(tempSlice.ndim(), 0)) << std::endl;

                std::cout << "Full slice contents: " << tempSlice << std::endl;

                // Define the specific coordinate
                // Assuming a 2D image. If 4D, use casacore::IPosition(4, 158, 172, 0, 0)
                int x = 158, y = 172;
                casacore::IPosition coord(ndim, 0);
                x = 0;
                y = 0;
                coord(0) = x; // X
                coord(1) = y; // Y
                // Set other dimensions (Stokes, Freq) to 0 if they exist

                casacore::Slicer singlePixelSlicer(coord, coord, casacore::Slicer::endIsLast);
                casacore::Array<float> pixelArray = _image->getSlice(singlePixelSlicer);

                // Accessing the result (the array is size 1, so index is 0)
                std::cout << "Value via Slicer: " << pixelArray(casacore::IPosition(ndim, 0)) << std::endl;
                */
            } else {
                _image.reset(new casacore::PagedImage<float>(_filename));
            }
//            printf("DEBUG : read image %s using ADIOSImage<float> class, isADIOS=%d\n", _filename.c_str(), isADIOS);
//            fflush(stdout);
#else
            _image.reset(new casacore::PagedImage<float>(_filename));
#endif
        } catch (const casacore::AipsError& err) {
            printf("EXCEPTION CAUGHT !!!\n");
            if (err.getMesg().startsWith("Invalid Table data type")) {
                // Temporary workaround (no data) to support complex images in file browser; must use LEL expression for data
                auto lattice = casacore::ImageOpener::openImage(_filename);
                _image.reset(ConvertImageToFloat(lattice));
                _data_type = lattice->dataType();
                delete lattice;
                converted = true;
            }
        }

        if (!_image) {
            throw(casacore::AipsError("Error opening image"));
        }

        _image_shape = _image->shape();
        _num_dims = _image_shape.size();
        _has_pixel_mask = _image->hasPixelMask();
        _coord_sys = std::shared_ptr<casacore::CoordinateSystem>(static_cast<casacore::CoordinateSystem*>(_image->coordinates().clone()));

        if (!converted) {
            _data_type = _image->dataType();
        }
    }
}

casacore::TempImage<float>* CasaLoader::ConvertImageToFloat(casacore::LatticeBase* lattice) {
    // Create a TempImage with no data
    if (!lattice) {
        // Not supported by ImageOpener
        throw(casacore::AipsError("Image data type not supported."));
    }

    casacore::TempImage<float>* float_image(nullptr);

    switch (lattice->dataType()) {
        case casacore::TpBool: { // Not supported by ImageOpener
            casacore::PagedImage<bool>* bool_image = dynamic_cast<casacore::PagedImage<bool>*>(lattice);
            float_image = new casacore::TempImage<float>(bool_image->shape(), bool_image->coordinates());
            float_image->setUnits(bool_image->units());
            float_image->setMiscInfo(bool_image->miscInfo());
            float_image->setImageInfo(bool_image->imageInfo());
            break;
        }
        case casacore::TpInt: { // Not supported by ImageOpener
            casacore::PagedImage<int>* int_image = dynamic_cast<casacore::PagedImage<int>*>(lattice);
            float_image = new casacore::TempImage<float>(int_image->shape(), int_image->coordinates());
            float_image->setUnits(int_image->units());
            float_image->setMiscInfo(int_image->miscInfo());
            float_image->setImageInfo(int_image->imageInfo());
            break;
        }
        case casacore::TpDouble: {
            casacore::PagedImage<double>* double_image = dynamic_cast<casacore::PagedImage<double>*>(lattice);
            float_image = new casacore::TempImage<float>(double_image->shape(), double_image->coordinates());
            float_image->setUnits(double_image->units());
            float_image->setMiscInfo(double_image->miscInfo());
            float_image->setImageInfo(double_image->imageInfo());
            break;
        }
        case casacore::TpComplex: {
            casacore::PagedImage<casacore::Complex>* complex_image = dynamic_cast<casacore::PagedImage<casacore::Complex>*>(lattice);
            float_image = new casacore::TempImage<float>(complex_image->shape(), complex_image->coordinates());
            float_image->setUnits(complex_image->units());
            float_image->setMiscInfo(complex_image->miscInfo());
            float_image->setImageInfo(complex_image->imageInfo());
            break;
        }
        case casacore::TpDComplex: {
            casacore::PagedImage<casacore::DComplex>* complex_image = dynamic_cast<casacore::PagedImage<casacore::DComplex>*>(lattice);
            float_image = new casacore::TempImage<float>(complex_image->shape(), complex_image->coordinates());
            float_image->setUnits(complex_image->units());
            float_image->setMiscInfo(complex_image->miscInfo());
            float_image->setImageInfo(complex_image->imageInfo());
            break;
        }
        default:
            throw(casacore::AipsError("Image data type not supported."));
    }

    return float_image;
}

} // namespace carta

#endif // CARTA_SRC_IMAGEDATA_CASALOADER_H_
