/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021, 2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "CompressedFits.h"

#include <chrono>
#include <cmath>

#include "../Logger/Logger.h"
#include "Util/FileSystem.h"
#include "Util/String.h"

#define FITS_BLOCK_SIZE 2880
#define FITS_CARD_SIZE 80
#define INITIAL_HEADERS_SIZE 4

using namespace carta;

CompressedFits::CompressedFits(const std::string& filename) : _filename(filename) {
    // Initialize linear transformation matrix for the direction coordinate
    SetDefaultTransformMatrix();
}

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

    bool in_image_headers(false), in_beam_headers(false);
    long long data_size(1);
    size_t buffer_index(0);
    BeamInfo beam_info;
    BeamTableInfo beam_table_info;
    casacore::String beam_unit("deg");

    auto t_start_get_hdu = std::chrono::high_resolution_clock::now();

    while (!gzeof(zip_file)) {
        // Read headers
        size_t bufsize(FITS_BLOCK_SIZE);
        int err(0);
        std::string buffer(bufsize, 0);
        size_t bytes_read = gzread(zip_file, buffer.data(), bufsize);
        buffer_index = 0;

        if (bytes_read == -1) {
            const char* error_string = gzerror(zip_file, &err);
            spdlog::debug("gzread failed with error {}", error_string);
            spdlog::error("Error reading gz file into buffer");

            gzclose(zip_file);
            return false;
        }

        if ((buffer.substr(0, 6) == "SIMPLE") || (buffer.substr(0, 8) == "XTENSION")) {
            // New hdu
            hdu++;
            data_size = 1;
            beam_info.clear();
            beam_table_info.clear();

            // Read initial headers to determine if image
            in_image_headers = IsImageHdu(buffer, file_info_ext, data_size);
            buffer_index = INITIAL_HEADERS_SIZE * FITS_CARD_SIZE;

            if (!in_image_headers) {
                file_info_ext.clear_header_entries();
                buffer.resize(bytes_read);

                // Read headers to determine if beam table and set info
                in_beam_headers = IsBeamTable(buffer, beam_table_info);
                if (in_beam_headers && beam_table_info.is_defined()) {
                    ReadBeamsTable(zip_file, beam_table_info);
                    in_beam_headers = false;
                    beam_table_info.clear();
                    continue;
                }

                buffer_index = bytes_read;
            }
        }

        // Continue parsing headers and add to file info and/or data size to skip data
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
                if (in_image_headers) {
                    AddHeaderEntry(keyword, value, comment, file_info_ext);
                }

                if (in_beam_headers) {
                    if (keyword == "NCHAN") {
                        beam_table_info.nchan = std::stoi(value);
                    } else if (keyword == "NPOL") {
                        beam_table_info.npol = std::stoi(value);
                    }
                }

                // Determine data size and store beam info in image headers
                if (keyword.startsWith("NAXIS") && !in_beam_headers) {
                    try {
                        auto naxis = std::stoi(value);
                        data_size *= naxis;
                    } catch (std::invalid_argument) {
                        spdlog::debug("Invalid {} value: {}, skipping hdu {}", keyword, value, hdu);
                        file_info_ext.clear_header_entries();
                        in_image_headers = false;
                        break;
                    }
                } else if (keyword == "BMAJ") {
                    beam_info.bmaj = value + beam_unit;
                } else if (keyword == "BMIN") {
                    beam_info.bmin = value + beam_unit;
                } else if (keyword == "BPA") {
                    beam_info.bpa = value + beam_unit;
                }

                // Set linear transformation matrix between the pixel and world axes
                if (keyword.startsWith("PC") || keyword.startsWith("CD")) {
                    auto found = keyword.find("_");
                    if (found != casacore::String::npos) {
                        if (keyword.at(found - 1) == '1' && keyword.at(keyword.length() - 1) == '1') {
                            _xform(0, 0) = casacore::String::toDouble(value);
                        } else if (keyword.at(found - 1) == '1' && keyword.at(keyword.length() - 1) == '2') {
                            _xform(1, 0) = casacore::String::toDouble(value);
                        } else if (keyword.at(found - 1) == '2' && keyword.at(keyword.length() - 1) == '1') {
                            _xform(0, 1) = casacore::String::toDouble(value);
                        } else if (keyword.at(found - 1) == '2' && keyword.at(keyword.length() - 1) == '2') {
                            _xform(1, 1) = casacore::String::toDouble(value);
                        }
                    }
                }
            } else {
                // END of header
                if (in_image_headers) {
                    // Add entry to file info map
                    std::string hduname = std::to_string(hdu);
                    hdu_info_map[hduname] = file_info_ext;

                    // Add beam to beam set
                    if (beam_info.defined()) {
                        SetBeam(beam_info);
                    }
                }

                if (in_beam_headers) {
                    ReadBeamsTable(zip_file, beam_table_info);
                } else {
                    // Skip data blocks
                    if (data_size > 1) {
                        auto nblocks_data = std::ceil((float)data_size / (float)FITS_BLOCK_SIZE);
                        gzseek(zip_file, nblocks_data * FITS_BLOCK_SIZE, SEEK_CUR);
                    }
                }

                // Reset for next hdu
                file_info_ext.clear_header_entries();
                in_image_headers = false;
                in_beam_headers = false;

                // Stop parsing block
                break;
            }
        }
    }

    gzclose(zip_file);

    auto t_end_get_hdu_info = std::chrono::high_resolution_clock::now();
    auto dt_get_hdu_info = std::chrono::duration_cast<std::chrono::microseconds>(t_end_get_hdu_info - t_start_get_hdu_info).count();
    spdlog::performance("Get hdu info map in {:.3f} ms", dt_get_hdu_info * 1e-3);
    return true;
}

bool CompressedFits::GetFirstImageHdu(string& hduname) {
    // Read compressed file headers to get the first image HDU
    auto* zip_file = OpenGzFile();
    if (zip_file == Z_NULL) {
        return false;
    }

    auto t_start_get_first_image_hdu = std::chrono::high_resolution_clock::now();

    bool first_image_hdu_ok(false);
    bool in_image_headers(false);
    int hdu(-1);
    CARTA::FileInfoExtended file_info_ext;
    long long data_size(1);

    while (!gzeof(zip_file)) {
        // Read headers
        size_t bufsize(FITS_BLOCK_SIZE);
        std::string buffer(bufsize, 0);
        size_t bytes_read = gzread(zip_file, buffer.data(), bufsize);

        if ((buffer.substr(0, 6) == "SIMPLE") || (buffer.substr(0, 8) == "XTENSION")) {
            // New hdu
            hdu++;
            data_size = 1;

            // Read initial headers to determine if image
            if (IsImageHdu(buffer, file_info_ext, data_size)) {
                first_image_hdu_ok = true;
                hduname = std::to_string(hdu);
                break;
            }
        }
    }

    gzclose(zip_file);

    auto t_end_get_first_image_hdu = std::chrono::high_resolution_clock::now();
    auto dt_get_first_image_hdu =
        std::chrono::duration_cast<std::chrono::microseconds>(t_end_get_first_image_hdu - t_start_get_first_image_hdu).count();
    spdlog::performance("Get the first image hdu in {:.3f} ms", dt_get_first_image_hdu * 1e-3);
    return first_image_hdu_ok;
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

bool CompressedFits::IsImageHdu(const std::string& fits_block, CARTA::FileInfoExtended& file_info_ext, long long& data_size) {
    // Parse initial header strings (FITS standard: SIMPLE/XTENSION, BITPIX, NAXIS) to determine whether to include this HDU

    // Check first header value
    casacore::String header = fits_block.substr(0, FITS_CARD_SIZE);
    casacore::String keyword, value, comment;
    ParseFitsCard(header, keyword, value, comment);
    bool is_image = ((keyword == "SIMPLE") && (value == "T")) || ((keyword == "XTENSION") && (value == "IMAGE"));

    // Add header to file info
    if (is_image) {
        AddHeaderEntry(keyword, value, comment, file_info_ext);
    }

    // Check other initial headers and calculate data size (for skipping blocks)
    bool bitpix_ok(false), naxis_ok(false);
    for (auto i = 1; i < INITIAL_HEADERS_SIZE; ++i) {
        header = fits_block.substr(i * FITS_CARD_SIZE, FITS_CARD_SIZE);

        casacore::String keyword, value, comment;
        ParseFitsCard(header, keyword, value, comment);

        if (keyword == "BITPIX") {
            std::vector<casacore::String> valid_bitpix = {"8", "16", "32", "64", "-32", "-64"};
            auto found = std::find(std::begin(valid_bitpix), std::end(valid_bitpix), value);
            bitpix_ok = (found != std::end(valid_bitpix));

            try {
                auto bitpix = std::stoi(value);
                data_size *= abs(bitpix / 8);
            } catch (std::invalid_argument) {
                data_size = 0;
                return false;
            }
        } else if (keyword == "NAXIS") {
            try {
                auto naxis = std::stoi(value);
                naxis_ok = naxis >= 2;

                if (naxis == 0) {
                    data_size = 0;
                }
            } catch (std::invalid_argument) {
                data_size = 0;
                return false;
            }
        } else if (keyword.startsWith("NAXIS")) {
            try {
                auto naxis = std::stoi(value);
                data_size *= naxis;
            } catch (std::invalid_argument) {
                return false;
            }
        }

        if (is_image) {
            // Add header to file info
            AddHeaderEntry(keyword, value, comment, file_info_ext);
        }
    }

    return is_image && bitpix_ok && naxis_ok;
}

void CompressedFits::ParseFitsCard(
    casacore::String& fits_card, casacore::String& keyword, casacore::String& value, casacore::String& comment) {
    // Extract parts of FITS header.
    if (fits_card.empty()) {
        return;
    }

    if (fits_card.startsWith("HISTORY")) {
        // Do not parse HISTORY
        keyword = fits_card;
        return;
    }

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

bool CompressedFits::IsBeamTable(const std::string& fits_block, BeamTableInfo& beam_table_info) {
    // Read BINTABLE headers to determine whether it is a beam table
    bool is_beam_table(false);

    size_t buffer_index(0);
    while (buffer_index < fits_block.size()) {
        casacore::String fits_card = fits_block.substr(buffer_index, FITS_CARD_SIZE);
        buffer_index += FITS_CARD_SIZE;

        fits_card.trim();
        if (fits_card.empty()) {
            continue;
        }

        if (fits_card == "END") {
            break;
        }

        if (fits_card.startsWith("XTENSION") && (fits_card.find("BINTABLE") == std::string::npos)) {
            // Not BINTABLE extension
            beam_table_info.clear();
            return false;
        }

        if (fits_card.startsWith("EXTNAME")) {
            if (fits_card.find("BEAMS") == std::string::npos) {
                // Not BEAMS extension
                beam_table_info.clear();
                return false;
            } else {
                is_beam_table = true;
            }
        }

        casacore::String keyword, value, comment;
        ParseFitsCard(fits_card, keyword, value, comment);

        if (keyword == "NAXIS1") {
            beam_table_info.nbytes_per_row = std::stoi(value);
        } else if (keyword == "NAXIS2") {
            beam_table_info.nrow = std::stoi(value);
        } else if (keyword == "TFIELDS") {
            beam_table_info.ncol = std::stoi(value);
        } else if (keyword == "NCHAN") {
            beam_table_info.nchan = std::stoi(value);
        } else if (keyword == "NPOL") {
            beam_table_info.npol = std::stoi(value);
        } else if (keyword.startsWith("TTYPE") || keyword.startsWith("TUNIT")) {
            // Store column info
            std::string index_str(1, keyword.lastchar());
            int index = std::atoi(index_str.c_str());

            if (beam_table_info.column_info.size() < index) {
                beam_table_info.column_info.resize(index);
            }

            if (keyword.startsWith("TTYPE")) {
                beam_table_info.column_info[index - 1].name = value;
            } else if (keyword.startsWith("TUNIT")) {
                beam_table_info.column_info[index - 1].unit = value;
            }
        }
    }

    return is_beam_table;
}

void CompressedFits::SetBeam(BeamInfo& beam_info) {
    // Set beam in ImageBeamSet using beam info
    try {
        casacore::Quantity bmajq, bminq, bpaq;
        casacore::readQuantity(bmajq, beam_info.bmaj);
        casacore::readQuantity(bminq, beam_info.bmin);
        casacore::readQuantity(bpaq, beam_info.bpa);

        casacore::GaussianBeam beam(bmajq, bminq, bpaq);
        _beam_set = casacore::ImageBeamSet(beam);
    } catch (casacore::AipsError& err) {
        spdlog::debug("Failed to set beam information: {}", err.getMesg());
    }
}

void CompressedFits::ReadBeamsTable(gzFile zip_file, BeamTableInfo& beam_table_info) {
    // Read Beams table into ImageBeamSet.  Location of zip_file offset must be beginning of table.
    int nchan(beam_table_info.nchan), npol(beam_table_info.npol), nrow(beam_table_info.nrow);

    _beam_set.resize(nchan, npol);
    _beam_set.set(casacore::GaussianBeam::NULL_BEAM);

    size_t bufsize(FITS_BLOCK_SIZE);
    unsigned char buffer[bufsize];
    int err(0), nrow_read(0);

    while (nrow_read < beam_table_info.nrow) {
        size_t bytes_read = gzread(zip_file, buffer, bufsize);

        auto nrow_in_block = bytes_read / beam_table_info.nbytes_per_row;
        auto nrow_to_read = nrow - nrow_read;
        if (nrow_in_block > nrow_to_read) {
            nrow_in_block = nrow_to_read;
        }

        nrow_read += nrow_in_block;
        int buffer_index(0);

        for (int i = 0; i < nrow_in_block; ++i) {
            casacore::Quantity bmajq, bminq, bpaq;
            int chan, pol;

            for (auto& column : beam_table_info.column_info) {
                unsigned char num_buffer[4];
#if defined(AIPS_LITTLE_ENDIAN)
                num_buffer[3] = buffer[buffer_index++];
                num_buffer[2] = buffer[buffer_index++];
                num_buffer[1] = buffer[buffer_index++];
                num_buffer[0] = buffer[buffer_index++];
#else
                num_buffer[0] = buffer[buffer_index++];
                num_buffer[1] = buffer[buffer_index++];
                num_buffer[2] = buffer[buffer_index++];
                num_buffer[3] = buffer[buffer_index++];
#endif

                std::string column_name(column.name);
                if (column_name == "BMAJ") {
                    float fval = (*(float*)num_buffer);
                    bmajq = casacore::Quantity(fval, column.unit);
                } else if (column_name == "BMIN") {
                    float fval = (*(float*)num_buffer);
                    bminq = casacore::Quantity(fval, column.unit);
                } else if (column_name == "BPA") {
                    float fval = (*(float*)num_buffer);
                    bpaq = casacore::Quantity(fval, column.unit);
                } else if (column_name == "CHAN") {
                    int ival = (*(int*)num_buffer);
                    chan = ival;
                } else if (column_name == "POL") {
                    int ival = (*(int*)num_buffer);
                    pol = ival;
                }
            }

            casacore::GaussianBeam beam(bmajq, bminq, bpaq);
            _beam_set.setBeam(chan, pol, beam);
        }
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
    size_t bufsize(FITS_BLOCK_SIZE);
    unsigned long long unzip_size(0);

    bool in_hdu(false);
    int data_size(1);

    while (!gzeof(zip_file)) {
        // Read 1 byte, seek to end of block
        int err(0);
        std::string buffer(bufsize, 0);
        size_t bytes_read = gzread(zip_file, buffer.data(), bufsize);
        unzip_size += bytes_read;

        if (bytes_read == -1) {
            gzclose(zip_file);

            const char* error_string = gzerror(zip_file, &err);
            spdlog::debug("gzread failed with error: {}", error_string);
            spdlog::error("Error reading buffer for FITS gz file.");
            return 0;
        }

        if (!in_hdu && ((buffer.substr(0, 6) == "SIMPLE") || (buffer.substr(0, 8) == "XTENSION"))) {
            in_hdu = true;
            data_size = 1;
        }

        if (in_hdu) {
            int buffer_index(0);
            while (buffer_index < bytes_read) {
                casacore::String fits_card = buffer.substr(buffer_index, FITS_CARD_SIZE);
                buffer_index += FITS_CARD_SIZE;

                casacore::String keyword, value, comment;
                ParseFitsCard(fits_card, keyword, value, comment);

                if (keyword.startsWith("NAXIS") || (keyword == "BITPIX")) {
                    if (keyword == "BITPIX") {
                        auto bitpix = std::stoi(value);
                        data_size *= abs(bitpix / 8);
                    } else if (keyword == "NAXIS") {
                        auto naxis = std::stoi(value);
                        if (naxis == 0) {
                            data_size = 0;
                        }
                    } else if (keyword.startsWith("NAXIS")) {
                        auto naxis = std::stoi(value);
                        data_size *= naxis;
                    }
                } else if (keyword == "END") {
                    // Skip data blocks
                    if (data_size > 1) {
                        auto nblocks_data = std::ceil((float)data_size / (float)FITS_BLOCK_SIZE);
                        auto blocks_size = nblocks_data * FITS_BLOCK_SIZE;
                        unzip_size += blocks_size;
                        gzseek(zip_file, blocks_size, SEEK_CUR);
                    }

                    // Reset for next hdu
                    in_hdu = false;

                    // Stop parsing block
                    break;
                }
            }
        }
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
    spdlog::info("Decompressing FITS file to {}", _unzip_filename);
    std::ofstream out_file(_unzip_filename, std::ios_base::out | std::ios_base::binary);

    // Read and decompress file, write to output file
    // Set large buffer size
    int err(0);
    size_t bufsize(FITS_BLOCK_SIZE);

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
    std::error_code error_code;

    if (fs::exists(unzip_path, error_code)) {
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

casacore::Matrix<casacore::Double> CompressedFits::GetTransformMatrix() {
    // Check the validity of transform matrix
    double det2 = _xform(0, 0) * _xform(1, 1) - _xform(1, 0) * _xform(0, 1);
    if (det2 < 0 || std::fabs(std::sqrt(det2) - 1.0) > 1e-3) {
        SetDefaultTransformMatrix();
    }
    return _xform;
}

void CompressedFits::SetDefaultTransformMatrix() {
    _xform.resize(2, 2);
    _xform = 0.0;
    _xform.diagonal() = 1.0;
}
