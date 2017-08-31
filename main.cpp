#include <iostream>
#include <vector>
#include <algorithm>
#include <regex>

#include <fmt/format.h>
#include <fmt/ostream.h>
#include <highfive/H5File.hpp>
#include <mpi.h>

void CalculateStatus(int mpi_rank, int mpi_size, const HighFive::File &file, int width, int height, int depth, int x_offset, int y_offset, int z_offset);

using namespace std;
using namespace HighFive;

int main(int argc, char **argv) {
    int mpi_rank, mpi_size;
    // initialize MPI
    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &mpi_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);
    //float* payloadGather = nullptr;

//	char processor_name[MPI_MAX_PROCESSOR_NAME];
//	int name_len;
//	MPI_Get_processor_name(processor_name, &name_len);


    char filename[255];
    int w, h, x, y = 0;
    if (mpi_rank == 0) {
        fmt::print("Enter filename for reading: ");
        cin >> filename;
    }
    // "/home/angus/Downloads/L13591_sky.h5"
    MPI_Bcast(filename, 255, MPI_UNSIGNED_CHAR, 0, MPI_COMM_WORLD);
    File file(filename, File::ReadOnly);
    vector<string> file_object_list = file.listObjectNames();
    regex image_group_regex("Image\\d+");
    auto num_bands = count_if(file_object_list.begin(), file_object_list.end(), [image_group_regex](string s) { return regex_search(s, image_group_regex) > 0; });
    if (mpi_rank == 0)
        fmt::print("Opened file {} with {} slices\n", filename, num_bands);
    int dims[] = {0, 0, 0, 0, 0, 0};
    while (true) {
        if (mpi_rank == 0) {
            fmt::print("Enter width, height, depth, x-, y-, and z-offsets of region: ");
            cin >> dims[0] >> dims[1] >> dims[2] >> dims[3] >> dims[4] >> dims[5];
            fmt::print("Stats for region {}x{}x{}, offset @{},{},{}:\n", dims[0], dims[1], dims[2], dims[3], dims[4], dims[5]);
        }
        MPI_Bcast(dims, 4, MPI_INT, 0, MPI_COMM_WORLD);

        CalculateStatus(mpi_rank, mpi_size, file, dims[0], dims[1], dims[2], dims[3], dims[4], dims[5]);
    }


}

void CalculateStatus(int mpi_rank, int mpi_size, const HighFive::File &file, int width, int height, int depth, int x_offset, int y_offset, int z_offset) {
    std::vector<std::vector<float>> data_vector;
    vector<float> payload_gather;

    float node_max = ::std::numeric_limits<float>::lowest();
    float node_min = ::std::numeric_limits<float>::max();
    float node_sumX = 0;
    float node_sumX2 = 0;
    long node_num_elements = 0;


    for (auto band = z_offset; band < z_offset + depth; band++) {
        if (band % mpi_size != mpi_rank) {
            continue;
        }

        Group group = file.getGroup(fmt::format("Image{0:03d}/skyData", band));
        DataSet data_set = group.getDataSet(fmt::format("ImageDataArray_SB{0:03d}", band));
        DataSpace data_space = data_set.getSpace();
        auto dims = data_space.getDimensions();
        auto dimX = dims[0];
        auto dimY = dims[1];

        if (width == dimX && height == dimY)
            data_set.read(data_vector);
        else
            data_set.select({x_offset, y_offset}, {width, height}).read(data_vector);

        float maxVal = ::std::numeric_limits<float>::lowest();
        float minVal = ::std::numeric_limits<float>::max();
        float sumX = 0;
        float sumX2 = 0;
        for (auto &row: data_vector) {
            for (auto &element: row) {
                maxVal = max(maxVal, element);
                minVal = min(minVal, element);
                sumX += element;
                sumX2 += element * element;
            }
        }
        long numElements = dimX * dimY;
        float muX = sumX / max(1L, numElements);
        node_max = max(node_max, maxVal);
        node_min = min(node_min, minVal);
        node_sumX += sumX;
        node_sumX2 += sumX2;
        node_num_elements += numElements;
    }

    float payload[] = {node_min, node_max, node_sumX, node_sumX2, node_num_elements};
    int payload_elements = sizeof(payload) / sizeof(float);
    if (mpi_rank == 0) {
        payload_gather.resize(mpi_size * payload_elements, 0.0f);
    }

    MPI_Gather(payload, sizeof(payload) / sizeof(float), MPI_FLOAT, payload_gather.data(), payload_elements, MPI_FLOAT, 0, MPI_COMM_WORLD);

    if (mpi_rank == 0) {
        float global_max = ::std::numeric_limits<float>::lowest();
        float global_min = ::std::numeric_limits<float>::max();
        float global_sumX = 0;
        float global_sumX2 = 0;
        float global_num_elements = 0;

        for (auto i = 0; i < mpi_size * payload_elements; i += payload_elements) {
            global_min = min(global_min, payload_gather[i]);
            global_max = max(global_max, payload_gather[i + 1]);
            global_sumX += payload_gather[i + 2];
            global_sumX2 += payload_gather[i + 3];
            global_num_elements += payload_gather[i + 4];
        }
        float global_muX = global_sumX / max(1.0f, global_num_elements);
        float global_std_dev = sqrtf(global_sumX2 / max(1.0f, global_num_elements) - global_muX * global_muX);
        fmt::print("Global: Min: {}, Max: {}, Average: {}, StdDev: {}\n\n", global_min, global_max, global_muX, global_std_dev);
    }
}