/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "CompressedFits.h"

#include <chrono>

#include "../Logger/Logger.h"
#include "../Util.h"

#define FITS_BLOCK_SIZE 2880
#define FITS_CARD_SIZE 80
#define INITIAL_HEADERS_SIZE 4

#ifdef _BOOST_FILESYSTEM_
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

using namespace carta;

bool CompressedFits::GetFitsHeaderInfo(std::map<std::string, CARTA::FileInfoExtended>& hdu_info_map) {
    // Read compressed file headers to fill map
    auto zip_file = OpenGzFile();
    if (zip_file == Z_NULL) {
        return false;
    }

    auto t_start_get_hdu_info = std::chrono::high_resolution_clock::now();

    // For map:
    int hdu(-1);
    CARTA::FileInfoExtended file_info_ext;

    bool in_image_headers(false);
    auto t_start_get_hdu = std::chrono::high_resolution_clock::now();

    while (!gzeof(zip_file)) {
        size_t bufsize;
        if (in_image_headers) {
            // Read entire block for headers
            bufsize = FITS_BLOCK_SIZE;
        } else {
            // Read beginning of block to check for new HDU
            bufsize = FITS_CARD_SIZE * INITIAL_HEADERS_SIZE;
        }

        // Read headers
        int err(0);
        std::string buffer(bufsize, 0);
        size_t bytes_read = gzread(zip_file, buffer.data(), bufsize);

        if (bytes_read == -1) {
            const char* error_string = gzerror(zip_file, &err);
            spdlog::debug("gzread failed with error {}", error_string);
            spdlog::error("Error reading gz file into buffer");
            return false;
        }

        if ((buffer.substr(0, 6) == "SIMPLE") || (buffer.substr(0, 8) == "XTENSION")) {
            // New hdu: read initial headers
            t_start_get_hdu = std::chrono::high_resolution_clock::now();
            hdu++;
            in_image_headers = IsImageHdu(buffer, file_info_ext);

            if (!in_image_headers) {
                file_info_ext.clear_header_entries();
            }
        }

        // Size of rest of block - to read or skip
        size_t block_size = FITS_BLOCK_SIZE - bufsize;

        if (in_image_headers) {
            if (block_size > 0) {
                // Read rest of block after initial headers
                buffer.resize(block_size, 0);
                bytes_read = gzread(zip_file, buffer.data(), block_size);
            }

            // Continue parsing headers and add to file info
            size_t buffer_index(0);
            while (buffer_index < bytes_read) {
                casacore::String fits_card = buffer.substr(buffer_index, FITS_CARD_SIZE);
                buffer_index += FITS_CARD_SIZE;

                fits_card.trim();
                if (fits_card.empty()) {
                    continue;
                }

                casacore::String keyword, value, comment;
                ParseFitsCard(fits_card, keyword, value, comment);

                if (keyword != "END") {
                    AddHeaderEntry(keyword, value, comment, file_info_ext);
                } else {
                    // At last header "END", add entry to map
                    std::string hduname = std::to_string(hdu);
                    hdu_info_map[hduname] = file_info_ext;

                    auto t_end_get_hdu = std::chrono::high_resolution_clock::now();
                    auto dt_get_hdu = std::chrono::duration_cast<std::chrono::microseconds>(t_end_get_hdu - t_start_get_hdu).count();
                    spdlog::performance("Get hdu {} headers in {:.3f} ms", hdu, dt_get_hdu * 1e-3);

                    // Reset for next hdu
                    file_info_ext.clear_header_entries();
                    in_image_headers = false;

                    // Stop parsing block
                    break;
                }
            }
        } else {
            // Skip to end of block
            gzseek(zip_file, block_size, SEEK_CUR);
        }
    }

    gzclose(zip_file);

    auto t_end_get_hdu_info = std::chrono::high_resolution_clock::now();
    auto dt_get_hdu_info = std::chrono::duration_cast<std::chrono::microseconds>(t_end_get_hdu_info - t_start_get_hdu_info).count();
    spdlog::performance("Get hdu info in {:.3f} ms", dt_get_hdu_info * 1e-3);
    return true;
}

gzFile CompressedFits::OpenGzFile() {
    // Open input zip file and set buffer size
    gzFile zip_file = gzopen(_filename.c_str(), "rb");

    if (zip_file == Z_NULL) {
        spdlog::error("Error opening {}: {}", _filename, strerror(errno));
    } else {
        // Set buffer size
        int err(0);
        size_t bufsize(FITS_BLOCK_SIZE);
        int success = gzbuffer(zip_file, bufsize);

        if (success == -1) {
            const char* error_string = gzerror(zip_file, &err);
            spdlog::debug("gzbuffer size {} failed with error: {}", bufsize, error_string);
            spdlog::error("Error setting buffer for FITS gz file.");
            gzclose(zip_file);
            zip_file = Z_NULL;
        }
    }

    return zip_file;
}

bool CompressedFits::IsImageHdu(std::string& fits_block, CARTA::FileInfoExtended& file_info_ext) {
    // Parse initial header strings (FITS standard: SIMPLE/XTENSION, BITPIX, NAXIS) to determine whether to include this HDU

    // Check first header value
    casacore::String header = fits_block.substr(0, FITS_CARD_SIZE);
    casacore::String keyword, value, comment;
    ParseFitsCard(header, keyword, value, comment);
    bool is_image = ((keyword == "SIMPLE") && (value == "T")) || ((keyword == "XTENSION") && (value == "IMAGE"));

    if (!is_image) {
        return false;
    }

    // Add header to file info
    AddHeaderEntry(keyword, value, comment, file_info_ext);

    // Check other initial headers
    bool bitpix_ok(false), naxis_ok(false);
    for (auto i = 1; i < INITIAL_HEADERS_SIZE; ++i) {
        header = fits_block.substr(i * FITS_CARD_SIZE, FITS_CARD_SIZE);

        casacore::String keyword, value, comment;
        ParseFitsCard(header, keyword, value, comment);

        if (keyword == "BITPIX") {
            std::vector<casacore::String> valid_bitpix = {"8", "16", "32", "64", "-32", "-64"};
            auto found = std::find(std::begin(valid_bitpix), std::end(valid_bitpix), value);
            bitpix_ok = (found != std::end(valid_bitpix));

            if (!bitpix_ok) {
                return false;
            }
        } else if (keyword == "NAXIS") {
            try {
                naxis_ok = std::stoi(value) >= 2;
            } catch (std::invalid_argument) {
                // stoi failed
            }

            if (!naxis_ok) {
                return false;
            }
        }

        // Add header to file info
        AddHeaderEntry(keyword, value, comment, file_info_ext);
    }

    return bitpix_ok && naxis_ok;
}

void CompressedFits::ParseFitsCard(
    casacore::String& fits_card, casacore::String& keyword, casacore::String& value, casacore::String& comment) {
    // Extract parts of FITS header.

    // Split keyword, remainder of line
    std::vector<std::string> keyword_remainder;
    SplitString(fits_card, '=', keyword_remainder);
    keyword = keyword_remainder[0];
    keyword.trim();
    if (keyword[0] == '#') {
        comment = keyword;
        keyword = "";
        return;
    }

    if (keyword_remainder.size() > 1) {
        casacore::String remainder = keyword_remainder[1];
        remainder.trim();

        std::vector<std::string> value_comment;
        if (remainder.startsWith("'")) {
            auto end_pos = remainder.find("'", 1);
            value = remainder.substr(1, end_pos - 1);
            value.trim();
            remainder = remainder.after(end_pos);
            SplitString(remainder, '/', value_comment);
        } else {
            SplitString(remainder, '/', value_comment);
            value = value_comment[0];
            value.trim();
        }

        if (value_comment.size() > 1) {
            comment = value_comment[1];
            comment.trim();
        }
    }
}

void CompressedFits::AddHeaderEntry(
    casacore::String& keyword, casacore::String& value, casacore::String& comment, CARTA::FileInfoExtended& file_info_ext) {
    // Set CARTA::HeaderEntry fields in FileInfoExtended
    auto entry = file_info_ext.add_header_entries();
    entry->set_name(keyword);
    *entry->mutable_value() = value;

    if (!value.empty()) {
        // Set type, numeric value
        if (value.contains(".")) {
            try {
                // Set double value
                double dvalue = std::stod(value);
                entry->set_numeric_value(dvalue);
                entry->set_entry_type(CARTA::EntryType::FLOAT);
            } catch (std::invalid_argument) {
                // Set string value only
                entry->set_entry_type(CARTA::EntryType::STRING);
            }
        } else {
            try {
                // Set int value
                int ivalue = std::stoi(value);
                entry->set_numeric_value(ivalue);
                entry->set_entry_type(CARTA::EntryType::INT);
            } catch (std::invalid_argument) {
                // Set string value only
                entry->set_entry_type(CARTA::EntryType::STRING);
            }
        }
    }

    if (!comment.empty()) {
        entry->set_comment(comment);
    }
}

unsigned long long CompressedFits::GetDecompressSize() {
    // Returns size of decompressed gz file in kB

    // Check if file has already been decompressed and return size
    if (DecompressedFileExists()) {
        fs::path unzip_path(_unzip_filename);
        return fs::file_size(unzip_path) / 1000;
    }

    auto t_start_get_size = std::chrono::high_resolution_clock::now();

    auto zip_file = OpenGzFile();
    if (zip_file == Z_NULL) {
        return 0;
    }

    // Seek end of FITS blocks and accumulate size
    size_t seek_bufsize(FITS_BLOCK_SIZE - 1);
    unsigned long long unzip_size(0);
    std::string buffer(1, 0);

    while (!gzeof(zip_file)) {
        // Read 1 byte, seek to end of block
        int err(0);
        size_t bytes_read = gzread(zip_file, buffer.data(), 1);

        if (bytes_read == -1) {
            gzclose(zip_file);

            const char* error_string = gzerror(zip_file, &err);
            spdlog::debug("gzread failed with error: {}", error_string);
            spdlog::error("Error reading buffer for FITS gz file.");
            return 0;
        }

        size_t bytes_seek = gzseek(zip_file, seek_bufsize, SEEK_CUR);
        if (bytes_seek == -1) {
            gzclose(zip_file);

            const char* error_string = gzerror(zip_file, &err);
            spdlog::debug("gzseek failed with error: {}", error_string);
            spdlog::error("Error reading buffer for FITS gz file.");
            return 0;
        }

        unzip_size = bytes_seek;
    }

    gzclose(zip_file);

    auto t_end_get_size = std::chrono::high_resolution_clock::now();
    auto dt_get_size = std::chrono::duration_cast<std::chrono::microseconds>(t_end_get_size - t_start_get_size).count();
    spdlog::performance("Get decompressed fits.gz size in {:.3f} ms", dt_get_size * 1e-3);

    // Convert to kB
    return unzip_size / 1000;
}

bool CompressedFits::DecompressGzFile(std::string& unzip_filename, std::string& error) {
    // Decompress file to temp dir if needed.  Return unzip_filename or error.
    if (DecompressedFileExists()) {
        unzip_filename = _unzip_filename;
        return true;
    }

    if (_unzip_filename.empty()) {
        error = "Cannot determine temporary file path to decompress image.";
        return false;
    }

    auto t_start_decompress = std::chrono::high_resolution_clock::now();
    // Open input zip file and set buffer
    auto zip_file = OpenGzFile();

    // Open output fits file
    spdlog::debug("Decompressing FITS file to {}", _unzip_filename);
    std::ofstream out_file(_unzip_filename, std::ios_base::out | std::ios_base::binary);

    // Read and decompress file, write to output file
    size_t bufsize(FITS_BLOCK_SIZE);
    int err(0);

    while (!gzeof(zip_file)) {
        // Read and decompress file into buffer
        std::string buffer(bufsize, 0);
        size_t bytes_read = gzread(zip_file, buffer.data(), bufsize);

        if (bytes_read == -1) {
            const char* error_string = gzerror(zip_file, &err);
            spdlog::debug("gzread failed with error {}", error_string);
            error = "Error reading gz file.";
            return false;
        } else {
            out_file.write(buffer.c_str(), bytes_read);

            if (bytes_read < bufsize) {
                if (gzeof(zip_file)) {
                    break;
                } else {
                    const char* error_string = gzerror(zip_file, &err);
                    if (err != Z_OK) {
                        spdlog::debug("Error reading gz file: {}", error_string);

                        // Close gz file
                        gzclose(zip_file);

                        // Close and remove decompressed file
                        out_file.close();
                        fs::path out_path(_unzip_filename);
                        fs::remove(out_path);

                        error = "Error reading gz file.";
                        return false;
                    }
                }
            }
        }
    }

    gzclose(zip_file);
    out_file.close();
    unzip_filename = _unzip_filename;

    auto t_end_decompress = std::chrono::high_resolution_clock::now();
    auto dt_decompress = std::chrono::duration_cast<std::chrono::microseconds>(t_end_decompress - t_start_decompress).count();
    spdlog::performance("Decompress fits.gz in {:.3f} ms", dt_decompress * 1e-3);

    return true;
}

bool CompressedFits::DecompressedFileExists() {
    // Returns whether file exists with unzip filename
    SetDecompressFilename();

    fs::path unzip_path(_unzip_filename);
    if (fs::exists(unzip_path)) {
        // File already decompressed
        spdlog::info("Using decompressed FITS file {}", _unzip_filename);
        return true;
    }

    return false;
}

void CompressedFits::SetDecompressFilename() {
    // Determines temporary directory and filename with zip extension removed
    // Sets decompressed filename _unzip_filename to tmpdir/filename.fits.
    if (!_unzip_filename.empty()) {
        return;
    }

    auto tmp_path = fs::temp_directory_path();
    if (tmp_path.empty()) {
        return;
    }

    // Add filename.fits (remove .gz) to tmp path
    fs::path zip_path(_filename);
    tmp_path /= zip_path.filename().stem();

    _unzip_filename = tmp_path.string();
}
