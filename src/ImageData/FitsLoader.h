/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGEDATA_FITSLOADER_H_
#define CARTA_BACKEND_IMAGEDATA_FITSLOADER_H_

#include <casacore/casa/OS/HostInfo.h>
#include <casacore/images/Images/FITSImage.h>

#include "CartaFitsImage.h"
#include "CompressedFits.h"
#include "FileLoader.h"

#ifdef _BOOST_FILESYSTEM_
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

namespace carta {

class FitsLoader : public FileLoader {
public:
    FitsLoader(const std::string& filename, bool is_gz = false);
    ~FitsLoader();

    void OpenFile(const std::string& hdu) override;

private:
    std::string _unzip_file;
    casacore::uInt _hdu_num;
};

FitsLoader::FitsLoader(const std::string& filename, bool is_gz) : FileLoader(filename, is_gz) {}

FitsLoader::~FitsLoader() {
    // Remove decompressed fits.gz file
    auto unzip_path = fs::path(_unzip_file);
    if (fs::exists(unzip_path)) {
        fs::remove(unzip_path);
    }
}

void FitsLoader::OpenFile(const std::string& hdu) {
    // Convert string to FITS hdu number
    casacore::uInt hdu_num(FileInfo::GetFitsHdu(hdu));

    if (!_image || (hdu_num != _hdu_num)) {
        bool gz_mem_ok(true);

        if (_is_gz) {
            // Determine whether to load into memory or decompress on disk.
            // Do not check memory if for headers only.
            // Error if failure reading naxis/bitpix headers.
            CompressedFits fits_gz(_filename);
            auto required_mem_kB = fits_gz.GetDecompressSize();

            if (required_mem_kB == 0) {
                throw(casacore::AipsError("Error reading FITS gz file."));
            }

            auto free_mem_kB = casacore::HostInfo::memoryFree();
            gz_mem_ok = (free_mem_kB > required_mem_kB);
            spdlog::debug("required mem={} kB, free mem={} kB, access file in memory={}", required_mem_kB, free_mem_kB, gz_mem_ok);

            if (!gz_mem_ok) {
                std::string error;
                if (!fits_gz.DecompressGzFile(_unzip_file, error)) {
                    throw(casacore::AipsError("Decompress FITS gz file failed: " + error));
                }
            }
        }

        // Default is casacore::FITSImage; if fails, try CartaFitsImage
        bool use_casacore_fits(true);

        try {
            if (_is_gz) {
                if (gz_mem_ok) {
                    // Use cfitsio to access data in CartaFitsImage.
                    // casacore throws exception "No data in the zeroth or first extension"
                    use_casacore_fits = false;
                    _image.reset(new carta::CartaFitsImage(_filename, hdu_num));
                } else {
                    // use casacore for unzipped FITS file
                    _image.reset(new casacore::FITSImage(_unzip_file, 0, hdu_num));
                }
            } else {
                _image.reset(new casacore::FITSImage(_filename, 0, hdu_num));
            }
        } catch (const casacore::AipsError& err) {
            if (use_casacore_fits) {
                // casacore::FITSImage failed, try CartaFitsImage
                try {
                    _image.reset(new carta::CartaFitsImage(_filename, hdu_num));
                } catch (const casacore::AipsError& err) {
                    spdlog::error(err.getMesg());
                }
            } else {
                spdlog::error(err.getMesg());
            }
        }

        if (!_image) {
            throw(casacore::AipsError("Error loading FITS image."));
        }

        _hdu = hdu;
        _hdu_num = hdu_num;

        _image_shape = _image->shape();
        _num_dims = _image_shape.size();
        _has_pixel_mask = _image->hasPixelMask();
        _coord_sys = _image->coordinates();
    }
}

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_FITSLOADER_H_
