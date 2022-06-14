/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021, 2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND__COMPRESSION_H_
#define CARTA_BACKEND__COMPRESSION_H_

#include <cstddef>
#include <cstdint>
#include <vector>

namespace carta {

int Compress(std::vector<float>& array, size_t offset, std::vector<char>& compression_buffer, std::size_t& compressed_size, uint32_t nx,
    uint32_t ny, uint32_t precision);
int Decompress(std::vector<float>& array, std::vector<char>& compression_buffer, int nx, int ny, int precision);
std::vector<int32_t> GetNanEncodingsSimple(std::vector<float>& array, int offset, int length);
std::vector<int32_t> GetNanEncodingsBlock(std::vector<float>& array, int offset, int w, int h);

void RoundAndEncodeVertices(const std::vector<float>& array, std::vector<int32_t>& dest, float rounding_factor);
void EncodeIntegers(std::vector<int32_t>& array, bool strided = false);

} // namespace carta

#endif // CARTA_BACKEND__COMPRESSION_H_
