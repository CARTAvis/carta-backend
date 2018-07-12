#pragma once

#include <vector>
#include <cstdint>
#include <cstddef>

int compress(std::vector<float>& array, size_t offset, std::vector<char>& compressionBuffer, std::size_t& compressedSize, uint32_t nx, uint32_t ny, uint32_t precision);
int decompress(std::vector<float>& array, std::vector<char>& compressionBuffer, std::size_t& compressedSize, uint32_t nx, uint32_t ny, uint32_t precision);
std::vector<int32_t> getNanEncodingsSimple(std::vector<float>& array, int offset, int length);
std::vector<int32_t> getNanEncodingsBlock(std::vector<float>& array, int offset, int w, int h);
