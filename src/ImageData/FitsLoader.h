/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGEDATA_FITSLOADER_H_
#define CARTA_BACKEND_IMAGEDATA_FITSLOADER_H_

#include <casacore/casa/OS/HostInfo.h>
#include <casacore/images/Images/FITSImage.h>

#include "../Cfitsio.h"
#include "CartaFitsImage.h"
#include "FileLoader.h"

namespace carta {

class FitsLoader : public FileLoader {
public:
    FitsLoader(const std::string& filename, bool is_gz = false);

    void OpenFile(const std::string& hdu) override;

    bool HasData(FileInfo::Data ds) const override;
    ImageRef GetImage() override;

private:
    bool GzImageMemoryOk(const casacore::uInt hdu_num, std::string& error);

    bool _is_gz;
    casacore::uInt _hdu;
    std::string _uncompress_filename;
    std::unique_ptr<casacore::ImageInterface<float>> _image;
};

FitsLoader::FitsLoader(const std::string& filename, bool is_gz) : FileLoader(filename), _is_gz(is_gz), _hdu(-1) {}

void FitsLoader::OpenFile(const std::string& hdu) {
    // Convert string to FITS hdu number
    casacore::uInt hdu_num(FileInfo::GetFitsHdu(hdu));

    if (!_image || (hdu_num != _hdu)) {
        bool gz_mem_ok(false);

        if (_is_gz) {
            // Determine whether to load into memory or uncompress on disk.
            // Error if failure reading naxis/bitpix headers.
            std::string error;
            gz_mem_ok = GzImageMemoryOk(hdu_num, error);

            if (!error.empty()) {
                throw(casacore::AipsError(error));
            }

            if (!gz_mem_ok) {
                // TODO: unzip image to _uncompress_filename
                throw(casacore::AipsError("FITS gz image will not fit in memory."));
            }
        }

        // Default is casacore::FITSImage; if fails, try CartaFitsImage
        bool use_casacore_fits(true);

        try {
            if (_is_gz) {
                if (gz_mem_ok) {
                    // Use wcslib to access data in CartaFitsImage.
                    // casacore throws exception "No data in the zeroth or first extension"
                    use_casacore_fits = false;
                    _image.reset(new carta::CartaFitsImage(_filename, hdu_num));
                } else {
                    // use casacore for unzipped FITS file
                    _image.reset(new casacore::FITSImage(_uncompress_filename, 0, hdu_num));
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

        _hdu = hdu_num;
        _num_dims = _image->shape().size();
    }
}

bool FitsLoader::HasData(FileInfo::Data dl) const {
    switch (dl) {
        case FileInfo::Data::Image:
            return true;
        case FileInfo::Data::XY:
            return _num_dims >= 2;
        case FileInfo::Data::XYZ:
            return _num_dims >= 3;
        case FileInfo::Data::XYZW:
            return _num_dims >= 4;
        case FileInfo::Data::MASK:
            return ((_image != nullptr) && _image->hasPixelMask());
        default:
            break;
    }
    return false;
}

typename FitsLoader::ImageRef FitsLoader::GetImage() {
    return _image.get(); // nullptr if image not opened
}

bool FitsLoader::GzImageMemoryOk(const casacore::uInt hdu_num, std::string& error) {
    // Check uncompressed size to see if it will fit in memory.
    // If any headers fail, returns error.
    bool gz_mem_ok(false);

    fitsfile* fptr(nullptr);
    int status(0);
    fits_open_file(&fptr, _filename.c_str(), 0, &status);

    if (status) {
        error = "Error opening FITS gz file.";
        return gz_mem_ok;
    }

    // Advance to requested hdu
    int hdu(hdu_num + 1);
    int* hdutype(nullptr);
    status = 0;
    fits_movabs_hdu(fptr, hdu, hdutype, &status);

    if (status) {
        error = "Error advancing FITS gz file to requested HDU.";
        status = 0;
        fits_close_file(fptr, &status);
        return gz_mem_ok;
    }

    // Get image parameters: data type, dims
    int maxdim(4), bitpix(0), naxis(0);
    long naxes[maxdim];
    status = 0;
    fits_get_img_param(fptr, maxdim, &bitpix, &naxis, naxes, &status);

    if (status) {
        error = "Error retrieving image parameters.";
        status = 0;
        fits_close_file(fptr, &status);
        return gz_mem_ok;
    }

    // Calculate data size
    long datasize(1);
    for (int i = 0; i < naxis; ++i) {
        datasize *= naxes[i];
    }

    // Calculate size of each data item
    size_t nbytes(0);
    switch (bitpix) {
        case 8:
            nbytes = sizeof(char);
            break;
        case 16:
            nbytes = sizeof(short);
            break;
        case 32:
            nbytes = sizeof(int);
            break;
        case 64:
            nbytes = sizeof(long long);
            break;
        case -32:
            nbytes = sizeof(float);
            break;
        case -64:
            nbytes = sizeof(double);
            break;
    }

    if (nbytes == 0) {
        error = "Invalid datatype (bitpix).";
        status = 0;
        fits_close_file(fptr, &status);
        return gz_mem_ok;
    }

    // Compare required to available memory
    auto required_mem_kB = (datasize * nbytes) / 1000.0;
    auto avail_mem_kB = casacore::HostInfo::memoryFree();
    gz_mem_ok = (avail_mem_kB > required_mem_kB);
    // spdlog::debug("FITS gz hdu {}: required memory {} kB, available memory {} kB", hdu_num, required_mem_kB, avail_mem_kB);
    spdlog::debug("Access FITS gz in memory: {}", gz_mem_ok);

    status = 0;
    fits_close_file(fptr, &status);

    return gz_mem_ok;
}

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_FITSLOADER_H_
