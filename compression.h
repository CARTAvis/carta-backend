#pragma once

#include <vector>
#include <cstdint>
#include <cstddef>
int compress(std::vector<float>& array, std::vector<char>& compressionBuffer, size_t& zfpsize, uint32_t nx, uint32_t ny, uint32_t precision);
int decompress(std::vector<float>& array, std::vector<char>& compressionBuffer, size_t& zfpsize, uint32_t nx, uint32_t ny, uint32_t precision);
std::vector<int32_t> getNanEncodings(std::vector<float>& array);
