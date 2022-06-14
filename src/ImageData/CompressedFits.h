/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021, 2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_IMAGEDATA_COMPRESSEDFITS_H_
#define CARTA_BACKEND_IMAGEDATA_COMPRESSEDFITS_H_

#include <zlib.h>
#include <map>
#include <string>

#include <casacore/casa/BasicSL/String.h>
#include <casacore/casa/aipsenv.h>
#include <casacore/images/Images/ImageBeamSet.h>

#include <carta-protobuf/file_info.pb.h>

// Read compressed FITS file headers

namespace carta {

// Struct to hold beam header values
struct BeamInfo {
    std::string bmaj;
    std::string bmin;
    std::string bpa;

    bool defined() {
        return !bmaj.empty() && !bmin.empty() && !bpa.empty();
    }

    void clear() {
        bmaj.clear();
        bmin.clear();
        bpa.clear();
    }
};

struct BeamTableInfo {
    // Column info
    struct ColumnInfo {
        std::string name; // TTYPEn
        std::string unit; // TUNITn
    };

    int nbytes_per_row; // NAXIS1
    int nrow;           // NAXIS2
    int ncol;           // TFIELDS
    int nchan;          // NCHAN
    int npol;           // NPOL
    std::vector<ColumnInfo> column_info;

    bool is_defined() {
        return meta_defined() && columns_defined();
    }

    bool meta_defined() {
        return (nbytes_per_row > 0) && (nrow > 0) && (ncol > 0) && (nchan > 0) && (npol > 0);
    }

    bool columns_defined() {
        bool defined = !column_info.empty();
        if (defined) {
            for (auto& info : column_info) {
                std::string column_name(info.name);

                // Every column should have a name
                if (column_name.empty()) {
                    defined = false;
                    break;
                }

                // Beam columns should have units
                if (info.unit.empty() && ((column_name == "BMAJ") || (column_name == "BMIN") || (column_name == "BPA"))) {
                    defined = false;
                    break;
                }
            }
        }
        return defined;
    }

    void clear() {
        nbytes_per_row = nrow = ncol = nchan = npol = 0;
        column_info.clear();
    }
};

class CompressedFits {
public:
    CompressedFits(const std::string& filename);

    // Headers for file info
    bool GetFitsHeaderInfo(std::map<std::string, CARTA::FileInfoExtended>& hdu_info_map);
    bool GetFirstImageHdu(string& hduname);

    // Beams for file info and opening image
    inline const casacore::ImageBeamSet& GetBeamSet() {
        return _beam_set;
    }

    casacore::Matrix<casacore::Double> GetTransformMatrix();

    void SetShape(casacore::IPosition shape) {
        _shape = shape;
    }
    casacore::IPosition& GetShape() {
        return _shape;
    }
    void SetSpecSuffix(int spec_axis) {
        _spec_suffix = std::to_string(spec_axis + 1);
    }
    void SetStokesSuffix(int stokes_axis) {
        _stokes_suffix = std::to_string(stokes_axis + 1);
    }
    std::string GetSpecSuffix() {
        return _spec_suffix;
    }
    std::string GetStokesSuffix() {
        return _stokes_suffix;
    }

    // File decompression
    unsigned long long GetDecompressSize();
    bool DecompressGzFile(std::string& unzip_file, std::string& error);

private:
    gzFile OpenGzFile();
    bool DecompressedFileExists();
    void SetDecompressFilename();

    // Extended file info
    bool IsImageHdu(const std::string& fits_block, CARTA::FileInfoExtended& file_info_ext, long long& data_size);
    void ParseFitsCard(casacore::String& fits_card, casacore::String& keyword, casacore::String& value, casacore::String& comment);
    void AddHeaderEntry(
        casacore::String& keyword, casacore::String& value, casacore::String& comment, CARTA::FileInfoExtended& file_info_ext);

    // Image beam set
    bool IsBeamTable(const std::string& fits_block, BeamTableInfo& beam_table_info);
    void SetBeam(BeamInfo& beam_info);
    void ReadBeamsTable(gzFile zip_file, BeamTableInfo& beam_table_info);

    void SetDefaultTransformMatrix();

    std::string _filename;
    std::string _unzip_filename;
    casacore::ImageBeamSet _beam_set;
    casacore::Matrix<casacore::Double> _xform; // Linear transform matrix for the direction coordinate
    casacore::IPosition _shape;                // Image shape
    std::string _spec_suffix;                  // Spectral suffix from the header
    std::string _stokes_suffix;                // Stokes suffix from the header
};

} // namespace carta

#endif // CARTA_BACKEND_IMAGEDATA_COMPRESSEDFITS_H_
