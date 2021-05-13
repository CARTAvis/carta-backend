/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "CompressedFits.h"

#include <zlib.h>
#include <map>

#include "../Logger/Logger.h"
#include "../Util.h"

#define FITS_BLOCK_SIZE 2880
#define FITS_CARD_SIZE 80
#define INITIAL_HEADERS_SIZE 3

bool CompressedFits::GetFitsHeaderInfo(std::map<std::string, CARTA::FileInfoExtended>& hdu_info_map) {
    // Read compressed file headers to fill map
    // Open input zip file
    auto zip_file = gzopen(_filename.c_str(), "rb");
    if (zip_file == Z_NULL) {
        spdlog::debug("Error opening {}: {}", _filename, strerror(errno));
        return false;
    }

    // Set buffer size
    int err(0);
    size_t bufsize(FITS_BLOCK_SIZE);
    int success = gzbuffer(zip_file, bufsize);
    if (success == -1) {
        const char* error_string = gzerror(zip_file, &err);
        spdlog::debug("gzbuffer size {} failed with error: {}", bufsize, error_string);
        spdlog::error("Error setting buffer size for reading compressed file");
        return false;
    }

    // For map:
    int hdu(-1);
    CARTA::FileInfoExtended file_info_ext;

    bool is_image_hdu(false);
    while (!gzeof(zip_file)) {
        // Read block
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
                block_index = 3 * FITS_CARD_SIZE;
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
