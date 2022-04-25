/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGEDATA_FITSLOADER_H_
#define CARTA_BACKEND_IMAGEDATA_FITSLOADER_H_

#include <chrono>

#include <casacore/casa/OS/HostInfo.h>
#include <casacore/images/Images/FITSImage.h>

#include "CartaFitsImage.h"
#include "CompressedFits.h"
#include "FileLoader.h"
#include "Util/FileSystem.h"

namespace carta {

class FitsLoader : public FileLoader {
public:
    FitsLoader(const std::string& filename, bool is_gz = false);
    ~FitsLoader();

    void OpenFile(const std::string& hdu) override;
    bool AddHistory(const CARTA::MomentRequest& moment_request) const override;

private:
    std::string _unzip_file;
    casacore::uInt _hdu_num;

    void RemoveHistoryBeam(unsigned int hdu_num);
};

FitsLoader::FitsLoader(const std::string& filename, bool is_gz) : FileLoader(filename, "", is_gz) {}

FitsLoader::~FitsLoader() {
    // Remove decompressed fits.gz file
    auto unzip_path = fs::path(_unzip_file);
    std::error_code error_code;
    if (fs::exists(unzip_path, error_code)) {
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
                    _image.reset(new CartaFitsImage(_filename, hdu_num));
                } else {
                    // use casacore for unzipped FITS file
                    _image.reset(new casacore::FITSImage(_unzip_file, 0, hdu_num));
                }
            } else {
                _image.reset(new casacore::FITSImage(_filename, 0, hdu_num));
                RemoveHistoryBeam(hdu_num);
            }
        } catch (const casacore::AipsError& err) {
            if (use_casacore_fits) {
                // casacore::FITSImage failed, try CartaFitsImage
                try {
                    use_casacore_fits = false;
                    _image.reset(new CartaFitsImage(_filename, hdu_num));
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
        _coord_sys = std::shared_ptr<casacore::CoordinateSystem>(static_cast<casacore::CoordinateSystem*>(_image->coordinates().clone()));

        if (use_casacore_fits) {
            casacore::FITSImage* fits_image = dynamic_cast<casacore::FITSImage*>(_image.get());
            _data_type = fits_image->internalDataType();
        } else {
            CartaFitsImage* fits_image = dynamic_cast<CartaFitsImage*>(_image.get());
            _data_type = fits_image->internalDataType();
        }
    }
}

void FitsLoader::RemoveHistoryBeam(unsigned int hdu_num) {
    // Remove beam not in header entries
    auto image_info = _image->imageInfo();
    if (image_info.hasBeam() && image_info.getBeamSet().hasSingleBeam()) {
        // Check if beam headers exist
        fitsfile* fptr;
        int status(0), hdu(hdu_num + 1); // 1-based for FITS
        int* hdutype(nullptr);

        // Open file and move to hdu
        fits_open_file(&fptr, _filename.c_str(), 0, &status);
        fits_movabs_hdu(fptr, hdu, hdutype, &status);

        // Read headers
        std::string record(80, 0);
        int key_num(1);
        bool bmaj_found(false), bmin_found(false), bpa_found(false);

        while (status == 0 && !(bmaj_found && bmin_found && bpa_found)) {
            fits_read_record(fptr, key_num++, record.data(), &status);

            if (status == 0) {
                std::string keyword = record.substr(0, 4);
                bmaj_found |= (keyword == "BMAJ");
                bmin_found |= (keyword == "BMIN");
                bpa_found |= (keyword == "BPA ");
            }
        }

        // Close file
        status = 0;
        fits_close_file(fptr, &status);

        if (!(bmaj_found && bmin_found && bpa_found)) {
            // Beam headers missing, remove from image info
            image_info.removeRestoringBeam();
            _image->setImageInfo(image_info);
        }
    }
}

bool FitsLoader::AddHistory(const CARTA::MomentRequest& moment_request) const {
    // Set history of moments requests
    int z_min = moment_request.spectral_range().min();
    int z_max = moment_request.spectral_range().max();
    std::string history = "HISTORY moments spectral range [" + std::to_string(z_min) + ", " + std::to_string(z_max) + "]" + '\0';

    fitsfile* fptr;
    int status(0), hdu(_hdu_num + 1), hdutype;

    // Open file and write moments requests history to the header
    fits_open_file(&fptr, _filename.c_str(), READWRITE, &status);
    fits_movabs_hdu(fptr, hdu, &hdutype, &status);
    if (hdutype == IMAGE_HDU) {
        fits_write_record(fptr, history.c_str(), &status);
    }
    fits_close_file(fptr, &status);

    return (status == 0);
}

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_FITSLOADER_H_
