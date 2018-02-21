#pragma once

#include <vector>
#include <zfp.h>
#include <cstdint>

int compress(std::vector<float> array, std::vector<char>& compressionBuffer, size_t &zfpsize, uint nx, uint ny, uint precision);
int decompress(std::vector<float> array, std::vector<char>& compressionBuffer, size_t &zfpsize, uint nx, uint ny, uint precision);
std::vector<int32_t > getNanEncodings(std::vector<float> array);