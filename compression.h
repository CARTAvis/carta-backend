#pragma once

#include <vector>
#include <zfp.h>
#include <cstdint>

int compress(float *array, unsigned char *&compressionBuffer, size_t &zfpsize, uint nx, uint ny, uint precision);
int decompress(float *array, unsigned char *compressionBuffer, size_t &zfpsize, uint nx, uint ny, uint precision);
std::vector<int32_t > getNanEncodings(float *array, size_t length);
