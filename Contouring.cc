#include "Contouring.h"

#include <chrono>
#include <cmath>
#include <limits>
#include <vector>

#include <fmt/format.h>
#include <tbb/tbb.h>

using namespace std;

// Contour tracing code adapted from SAOImage DS9: https://github.com/SAOImageDS9/SAOImageDS9
void TraceSegment(const float* image, std::vector<bool>& visited, int64_t width, int64_t height, double scale, double offset, double level,
    int x_cell, int y_cell, int side, vector<float>& vertices) {
    int64_t i = x_cell;
    int64_t j = y_cell;
    int orig_side = side;

    bool first_iteration = true;
    bool done = (i < 0 || i >= width - 1 || (j < 0 && j >= height - 1));

    while (!done) {
        bool flag = false;
        double a = image[(j)*width + i];
        double b = image[(j)*width + i + 1];
        double c = image[(j + 1) * width + i + 1];
        double d = image[(j + 1) * width + i];

        double x = 0;
        double y = 0;
        if (first_iteration) {
            first_iteration = false;
            switch (side) {
                case Edge::TopEdge:
                    x = (level - a) / (b - a) + i;
                    y = j;
                    break;
                case Edge::RightEdge:
                    x = i + 1;
                    y = (level - b) / (c - b) + j;
                    break;
                case Edge::BottomEdge:
                    x = (level - c) / (d - c) + i;
                    y = j + 1;
                    break;
                case Edge::LeftEdge:
                    x = i;
                    y = (level - a) / (d - a) + j;
                    break;
                default:
                    break;
            }

        } else {
            if (side == Edge::TopEdge) {
                visited[j * width + i] = true;
            }

            do {
                if (++side == Edge::None) {
                    side = Edge::TopEdge;
                }

                switch (side) {
                    case Edge::TopEdge:
                        if (a >= level && level > b) {
                            flag = true;
                            x = (level - a) / (b - a) + i;
                            y = j;
                            j--;
                        }
                        break;
                    case Edge::RightEdge:
                        if (b >= level && level > c) {
                            flag = true;
                            x = i + 1;
                            y = (level - b) / (c - b) + j;
                            i++;
                        }
                        break;
                    case Edge::BottomEdge:
                        if (c >= level && level > d) {
                            flag = true;
                            x = (level - d) / (c - d) + i;
                            y = j + 1;
                            j++;
                        }
                        break;
                    case Edge::LeftEdge:
                        if (d >= level && level > a) {
                            flag = true;
                            x = i;
                            y = (level - a) / (d - a) + j;
                            i--;
                        }
                        break;
                    default:
                        break;
                }
            } while (!flag);

            if (++side == Edge::None) {
                side = Edge::TopEdge;
            }
            if (++side == Edge::None) {
                side = Edge::TopEdge;
            }
            if (i == x_cell && j == y_cell && side == orig_side) {
                done = true;
            }
            if (i < 0 || i >= width - 1 || j < 0 || j >= height - 1) {
                done = true;
            }
        }

        // Shift to pixel center
        double x_val = x + 0.5;
        double y_val = y + 0.5;
        vertices.push_back(scale * x_val + offset);
        vertices.push_back(scale * y_val + offset);
    }
}

void TraceLevel(const float* image, int64_t width, int64_t height, double scale, double offset, double level, vector<float>& vertices,
    vector<int32_t>& indices, int chunk_size, ContourCallback& partial_callback) {
    const int64_t num_pixels = width * height;
    const size_t vertex_cutoff = 2 * chunk_size;
    int64_t checked_pixels = 0;
    vector<bool> visited(num_pixels);
    int64_t i, j;

    auto test_for_chunk_overflow = [&]() {
        if (vertex_cutoff && vertices.size() > vertex_cutoff) {
            double progress = std::min(0.99, checked_pixels / double(num_pixels));
            partial_callback(level, progress, vertices, indices);
            vertices.clear();
            indices.clear();
        }
    };

    // Search TopEdge
    for (j = 0, i = 0; i < width - 1; i++) {
        if (image[(j)*width + i] < level && level <= image[(j)*width + i + 1]) {
            indices.push_back(vertices.size());
            TraceSegment(image, visited, width, height, scale, offset, level, i, j, Edge::TopEdge, vertices);
            test_for_chunk_overflow();
        }
        checked_pixels++;
    }

    // Search RightEdge
    for (j = 0; j < height - 1; j++) {
        if (image[(j)*width + i] < level && level <= image[(j + 1) * width + i]) {
            indices.push_back(vertices.size());
            TraceSegment(image, visited, width, height, scale, offset, level, i - 1, j, Edge::RightEdge, vertices);
            test_for_chunk_overflow();
        }
        checked_pixels++;
    }

    // Search Bottom
    for (i--; i >= 0; i--) {
        if (image[(j)*width + i + 1] < level && level <= image[(j)*width + i]) {
            indices.push_back(vertices.size());
            TraceSegment(image, visited, width, height, scale, offset, level, i, j - 1, Edge::BottomEdge, vertices);
            test_for_chunk_overflow();
        }
        checked_pixels++;
    }

    // Search Left
    for (i = 0, j--; j >= 0; j--) {
        if (image[(j + 1) * width + i] < level && level <= image[(j)*width + i]) {
            indices.push_back(vertices.size());
            TraceSegment(image, visited, width, height, scale, offset, level, i, j, Edge::LeftEdge, vertices);
            test_for_chunk_overflow();
        }
        checked_pixels++;
    }

    // Search each row of the image
    for (j = 1; j < height - 1; j++) {
        for (i = 0; i < width - 1; i++) {
            if (!visited[j * width + i] && image[(j)*width + i] < level && level <= image[(j)*width + i + 1]) {
                indices.push_back(vertices.size());
                TraceSegment(image, visited, width, height, scale, offset, level, i, j, TopEdge, vertices);
                test_for_chunk_overflow();
            }
            checked_pixels++;
        }
    }
    partial_callback(level, 1.0, vertices, indices);
}

void TraceContours(float* image, int64_t width, int64_t height, double scale, double offset, const std::vector<double>& levels,
    std::vector<std::vector<float>>& vertex_data, std::vector<std::vector<int32_t>>& index_data, int chunk_size,
    ContourCallback& partial_callback, bool verbose_logging) {
    auto t_start_contours = std::chrono::high_resolution_clock::now();
    vertex_data.resize(levels.size());
    index_data.resize(levels.size());

    int64_t num_pixels = width * height;
    for (int64_t i = 0; i < num_pixels; i++) {
        if (isnan(image[i])) {
            image[i] = -std::numeric_limits<float>::max();
        }
    }

    auto loop = [&](const tbb::blocked_range<int64_t>& r) {
        for (int64_t l = r.begin(); l < r.end(); l++) {
            vertex_data[l].clear();
            index_data[l].clear();
            TraceLevel(image, width, height, scale, offset, levels[l], vertex_data[l], index_data[l], chunk_size, partial_callback);
        }
    };

    tbb::parallel_for(tbb::blocked_range<int64_t>(0, levels.size()), loop);

    if (verbose_logging) {
        auto t_end_contours = std::chrono::high_resolution_clock::now();
        auto dt_contours = std::chrono::duration_cast<std::chrono::microseconds>(t_end_contours - t_start_contours).count();
        auto rate_contours = width * height / (double)dt_contours;
        int vertex_count = 0;
        int segment_count = 0;
        for (auto& vertices : vertex_data) {
            vertex_count += vertices.size();
        }
        for (auto& indices : index_data) {
            segment_count += indices.size();
        }

        fmt::print("Contoured {}x{} image in {} ms at {} MPix/s. Found {} vertices in {} segments across {} levels\n", width, height,
            dt_contours * 1e-3, rate_contours, vertex_count, segment_count, levels.size());
    }
}