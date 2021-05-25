/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "CompressedFits.h"

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

    // For map:
    int hdu(-1);
    CARTA::FileInfoExtended file_info_ext;
    size_t bufsize(FITS_BLOCK_SIZE);
    bool is_image_hdu(false);

    while (!gzeof(zip_file)) {
        // Read block
        int err(0);
        char buffer[bufsize] = {'\0'};
        size_t bytes_read = gzread(zip_file, buffer, bufsize);

        if (bytes_read == -1) {
            const char* error_string = gzerror(zip_file, &err);
            spdlog::debug("gzread failed with error {}", error_string);
            spdlog::error("Error reading gz file into buffer");
            return false;
        }

        buffer[bufsize] = '\0';
        std::string fits_block(buffer);
        size_t block_index(0);

        if ((fits_block.substr(0, 6) == "SIMPLE") || (fits_block.substr(0, 8) == "XTENSION")) {
            // New hdu
            hdu++;
            is_image_hdu = IsImageHdu(fits_block, file_info_ext);

            if (is_image_hdu) {
                block_index = INITIAL_HEADERS_SIZE * FITS_CARD_SIZE;
            }
        }

        if (is_image_hdu) {
            while (block_index < bytes_read) {
                casacore::String fits_card = fits_block.substr(block_index, FITS_CARD_SIZE);
                block_index += FITS_CARD_SIZE;

                fits_card.trim();
                if (fits_card.empty()) {
                    continue;
                }

                casacore::String keyword, value, comment;
                ParseFitsCard(fits_card, keyword, value, comment);

                if (keyword != "END") {
                    AddHeaderEntry(keyword, value, comment, file_info_ext);
                } else {
                    // At last header, add entry to map
                    std::string hduname = std::to_string(hdu);
                    hdu_info_map[hduname] = file_info_ext;

                    // Reset for next hdu
                    file_info_ext.clear_header_entries();
                    is_image_hdu = false;

                    break;
                }
            }
        }
    }

    gzclose(zip_file);
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
    std::unordered_map<std::string, casacore::String> first_header = {{"SIMPLE", "T"}, {"XTENSION", "IMAGE"}};
    std::vector<std::string> valid_bitpix = {"8", "16", "32", "64", "-32", "-64"};

    bool first_header_ok(false), bitpix_ok(false), naxis_ok(false);
    size_t header_start(0);

    for (auto i = 0; i < INITIAL_HEADERS_SIZE; ++i) {
        casacore::String header = fits_block.substr(header_start, FITS_CARD_SIZE);
        header_start += FITS_CARD_SIZE;

        casacore::String keyword, value, comment;
        ParseFitsCard(header, keyword, value, comment);

        if (first_header.count(keyword)) {
            first_header_ok = (value == first_header[keyword]);
        } else if (keyword == "BITPIX") {
            auto found = std::find(std::begin(valid_bitpix), std::end(valid_bitpix), value);
            bitpix_ok = found != std::end(valid_bitpix);
        } else if (keyword == "NAXIS") {
            try {
                naxis_ok = std::stoi(value) >= 2;
            } catch (std::invalid_argument) {
                // stoi failed
            }
        }

        AddHeaderEntry(keyword, value, comment, file_info_ext);
    }

    bool is_image = first_header_ok && bitpix_ok && naxis_ok;
    if (!is_image) {
        file_info_ext.clear_header_entries();
    }

    return is_image;
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
        fs::path unzip_path(_unzip_file);
        return fs::file_size(unzip_path);
    }

    // Decompress file in blocks and accumulate size
    auto zip_file = OpenGzFile();
    if (zip_file == Z_NULL) {
        return 0;
    }

    size_t bufsize(FITS_BLOCK_SIZE);
    unsigned long long unzip_size(0);
    while (!gzeof(zip_file)) {
        // Read block
        int err(0);
        char buffer[bufsize] = {'\0'};
        size_t bytes_read = gzread(zip_file, buffer, bufsize);

        if (bytes_read == -1) {
            const char* error_string = gzerror(zip_file, &err);
            spdlog::debug("gzread failed with error: {}", error_string);
            spdlog::error("Error reading buffer for FITS gz file.");
            return 0;
        }

        unzip_size += bytes_read;
    }

    gzclose(zip_file);

    // Convert to kB
    return unzip_size / 1000;
}

bool CompressedFits::DecompressGzFile(std::string& unzip_file, std::string& error) {
    // Decompress file to temp dir if needed.  Return unzip_file or error.
    if (DecompressedFileExists()) {
        unzip_file = _unzip_file;
        return true;
    }

    if (_unzip_file.empty()) {
        error = "Cannot determine temporary file path to decompress image.";
        return false;
    }

    // Open input zip file and set buffer
    auto zip_file = OpenGzFile();

    // Open output fits file
    spdlog::debug("Decompressing FITS file to {}", _unzip_file);
    std::ofstream out_file(_unzip_file, std::ios_base::out | std::ios_base::binary);

    // Read and decompress file, write to output file
    size_t bufsize(FITS_BLOCK_SIZE);
    int err(0);

    while (!gzeof(zip_file)) {
        // Read and decompress file into buffer
        char buffer[bufsize];
        size_t bytes_read = gzread(zip_file, buffer, bufsize);

        if (bytes_read == -1) {
            const char* error_string = gzerror(zip_file, &err);
            spdlog::debug("gzread failed with error {}", error_string);
            error = "Error reading gz file.";
            return false;
        } else {
            out_file.write(buffer, bytes_read);
            auto file_offset = gzoffset(zip_file);

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
                        fs::path out_path(unzip_file);
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

    unzip_file = _unzip_file;
    return true;
}

bool CompressedFits::DecompressedFileExists() {
    // Returns whether file exists with unzip filename
    SetDecompressFilename();

    fs::path unzip_path(_unzip_file);
    if (fs::exists(unzip_path)) {
        // File already decompressed
        spdlog::info("Using decompressed FITS file {}", _unzip_file);
        return true;
    }

    return false;
}

void CompressedFits::SetDecompressFilename() {
    // Determines temporary directory and filename with zip extension removed
    // Sets decompressed filename _unzip_file to tmpdir/filename.fits.
    if (!_unzip_file.empty()) {
        return;
    }

    auto tmp_path = fs::temp_directory_path();
    if (tmp_path.empty()) {
        return;
    }

    // Add filename.fits (remove .gz) to tmp path
    fs::path zip_path(_filename);
    tmp_path /= zip_path.filename().stem();

    _unzip_file = tmp_path.string();
}
