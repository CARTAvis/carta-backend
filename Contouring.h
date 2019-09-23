#ifndef CARTA_BACKEND__CONTOURING_H_
#define CARTA_BACKEND__CONTOURING_H_

#include <vector>
#include <cstdint>

enum Edge { TopEdge, RightEdge, BottomEdge, LeftEdge, None };

void TraceContourLevel(float* image, int width, int height, double scale, double offset, double level, std::vector<double>& vertex_data, std::vector<int32_t>& indices);
void TraceContours(float* image, int width, int height, double scale, double offset, const std::vector<double>& levels, std::vector<std::vector<float>>& vertex_data,
    std::vector<std::vector<int32_t>>& index_data);

#endif // CARTA_BACKEND__CONTOURING_H_
