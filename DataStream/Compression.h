#ifndef CARTA_BACKEND__COMPRESSION_H_
#define CARTA_BACKEND__COMPRESSION_H_

#include <cstddef>
#include <cstdint>
#include <vector>

int Compress(std::vector<float>& array, size_t offset, std::vector<char>& compression_buffer, std::size_t& compressed_size, uint32_t nx,
    uint32_t ny, uint32_t precision);
std::vector<int32_t> GetNanEncodingsSimple(std::vector<float>& array, int offset, int length);
std::vector<int32_t> GetNanEncodingsBlock(std::vector<float>& array, int offset, int w, int h);

void RoundAndEncodeVertices(const std::vector<float>& array, std::vector<int32_t>& dest, float rounding_factor);
void EncodeIntegers(std::vector<int32_t>& array, bool strided = false);
#endif // CARTA_BACKEND__COMPRESSION_H_
