/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGEDATA_FITSLOADER_H_
#define CARTA_BACKEND_IMAGEDATA_FITSLOADER_H_

#include <casacore/casa/OS/HostInfo.h>
#include <casacore/images/Images/FITSImage.h>
#include "zlib.h"

#include "../Cfitsio.h"
#include "CartaFitsImage.h"
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

    bool HasData(FileInfo::Data ds) const override;
    ImageRef GetImage() override;

private:
    bool CheckGzMemory(const casacore::uInt hdu_num, std::string& error);
    bool DecompressGzFile(std::string& gz_filename, std::string& error);

    bool _is_gz;
    casacore::uInt _hdu;
    fs::path _decompress_file;
    std::unique_ptr<casacore::ImageInterface<float>> _image;
};

FitsLoader::FitsLoader(const std::string& filename, bool is_gz) : FileLoader(filename), _is_gz(is_gz), _hdu(-1) {}

FitsLoader::~FitsLoader() {
    if (fs::exists(_decompress_file)) {
        fs::remove(_decompress_file);
    }
}

void FitsLoader::OpenFile(const std::string& hdu) {
    // Convert string to FITS hdu number
    casacore::uInt hdu_num(FileInfo::GetFitsHdu(hdu));

    if (!_image || (hdu_num != _hdu)) {
        bool gz_mem_ok(false);

        if (_is_gz) {
            // Determine whether to load into memory or decompress on disk.
            // Error if failure reading naxis/bitpix headers.
            std::string error;
            gz_mem_ok = CheckGzMemory(hdu_num, error);

            if (!error.empty()) {
                throw(casacore::AipsError(error));
            }

            if (!gz_mem_ok) {
                bool inflated = DecompressGzFile(_filename, error);
                if (!inflated) {
                    throw(casacore::AipsError(error));
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
                    _image.reset(new casacore::FITSImage(_decompress_file.string(), 0, hdu_num));
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

bool FitsLoader::CheckGzMemory(const casacore::uInt hdu_num, std::string& error) {
    // Check decompressed size to see if it will fit in memory.
    // If any headers fail, returns error.
    bool gz_mem_ok(false);

    fitsfile* fptr(nullptr);
    int status(0);
    fits_open_file(&fptr, _filename.c_str(), 0, &status);

    if (status > 0) {
        spdlog::debug("fits_open_file status={}", status);
        error = "Error opening FITS gz file.";
        return gz_mem_ok;
    }

    // Advance to requested hdu
    int hdu(hdu_num + 1);
    int* hdutype(nullptr);
    status = 0;
    fits_movabs_hdu(fptr, hdu, hdutype, &status);

    if (status > 0) {
        spdlog::debug("fits_movabs_hdu status={}", status);
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

    if (status > 0) {
        spdlog::debug("fits_get_img_param status={}", status);
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
    spdlog::debug("Accessing FITS gz in memory: {}", gz_mem_ok);

    status = 0;
    fits_close_file(fptr, &status);

    return gz_mem_ok;
}

bool FitsLoader::DecompressGzFile(std::string& gz_filename, std::string& error) {
    // Decompress fits.gz to temp dir and set _decompress_filename
    auto tmp_path = fs::temp_directory_path();
    if (tmp_path.empty()) {
        error = "Cannot determine temporary file path to decompress gz image.";
        return false;
    }

    // tmp path is tmpdir/filename.fits (remove .gz)
    fs::path gz_path(gz_filename);
    tmp_path /= gz_path.filename().stem();

    if (fs::exists(tmp_path)) {
        _decompress_file = tmp_path;
        spdlog::debug("Using decompressed FITS file {}", _decompress_file.string());
        return true;
    }

    std::string out_filename(tmp_path.string());
    spdlog::debug("Decompressing FITS file to {}", out_filename);
    std::ofstream out_file(out_filename, std::ios_base::out | std::ios_base::binary);

    // Open input gz file
    auto gz_file = gzopen(gz_filename.c_str(), "rb");
    if (gz_file == Z_NULL) {
        spdlog::debug("gzopen failed: {}", strerror(errno));
        error = "Error opening FITS gz file for decompression";
        return false;
    }

    // Set buffer size
    int err(0);
    size_t bufsize(64000);
    int success = gzbuffer(gz_file, bufsize);
    if (success == -1) {
        const char* error_string = gzerror(gz_file, &err);
        spdlog::debug("gzbuffer size {} failed with error: {}", bufsize, error_string);
        error = "Error setting buffer size for decompression";
        return false;
    }

    // Read and decompress file, write to output file
    while (!gzeof(gz_file)) {
        // Read and decompress file into buffer
        char buffer[bufsize];
        size_t bytes_read = gzread(gz_file, buffer, bufsize);

        if (bytes_read == -1) {
            const char* error_string = gzerror(gz_file, &err);
            spdlog::debug("gzread failed with error {}", error_string);
            error = "Error reading gz file into buffer";
            return false;
        } else {
            out_file.write(buffer, bytes_read);
            auto file_offset = gzoffset(gz_file);

            if (bytes_read < bufsize) {
                if (gzeof(gz_file)) {
                    break;
                } else {
                    const char* error_string = gzerror(gz_file, &err);
                    if (err != Z_OK) {
                        spdlog::debug("Error reading gz file: {}", error_string);

                        // Close gz file
                        gzclose(gz_file);

                        // Close and remove decompressed file
                        out_file.close();
                        fs::path out_path(out_filename);
                        fs::remove(out_path);

                        error = "Decompressing FITS file failed.";
                        return false;
                    }
                }
            }
        }
    }

    gzclose(gz_file);
    out_file.close();

    _decompress_file = tmp_path;
    return true;
}

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_FITSLOADER_H_
