#ifndef CARTA_BACKEND__CONTOURING_H_
#define CARTA_BACKEND__CONTOURING_H_

#include <cstdint>
#include <functional>
#include <vector>

typedef const std::function<void(double, double, const std::vector<float>&, const std::vector<int32_t>&)> ContourCallback;

enum Edge { TopEdge, RightEdge, BottomEdge, LeftEdge, None };

void TraceContourLevel(float* image, int64_t width, int64_t height, double scale, double offset, double level,
    std::vector<double>& vertex_data, std::vector<int32_t>& indices);
void TraceContours(float* image, int64_t width, int64_t height, double scale, double offset, const std::vector<double>& levels,
    std::vector<std::vector<float>>& vertex_data, std::vector<std::vector<int32_t>>& index_data, int chunk_size,
    ContourCallback& partial_callback, bool verbose_logging = false);

#endif // CARTA_BACKEND__CONTOURING_H_
