/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND__UTIL_FILE_H_
#define CARTA_BACKEND__UTIL_FILE_H_

// Valid for little-endian only
#define FITS_MAGIC_NUMBER 0x504D4953
#define GZ_MAGIC_NUMBER 0x08088B1F
#define HDF5_MAGIC_NUMBER 0x46444889
#define XML_MAGIC_NUMBER 0x6D783F3C

uint32_t GetMagicNumber(const std::string& filename);
bool IsCompressedFits(const std::string& filename);

// stokes types and value conversion
int GetStokesValue(const CARTA::StokesType& stokes_type);
CARTA::StokesType GetStokesType(int stokes_value);

#endif // CARTA_BACKEND__UTIL_FILE_H_
