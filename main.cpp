#include <iostream>
#include <vector>
#include <algorithm>
#include <regex>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <highfive/H5File.hpp>

#include <mpi.h>

void
calculateStats(int mpiRank, int mpiSize, const HighFive::File &file, int width, int height, int depth, int xOffset, int yOffset, int zOffset);

using namespace std;
using namespace HighFive;

int main(int argc, char **argv) {
    int mpiRank, mpiSize;
    // initialize MPI
    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &mpiSize);
    MPI_Comm_rank(MPI_COMM_WORLD, &mpiRank);
    //float* payloadGather = nullptr;

//	char processor_name[MPI_MAX_PROCESSOR_NAME];
//	int name_len;
//	MPI_Get_processor_name(processor_name, &name_len);


    char filename[255];
    int w, h, x, y = 0;
    if (mpiRank == 0) {
        fmt::print("Enter filename for reading: ");
        cin >> filename;
    }
    // "/home/angus/Downloads/L13591_sky.h5"
    MPI_Bcast(filename, 255, MPI_UNSIGNED_CHAR, 0, MPI_COMM_WORLD);
    File file(filename, File::ReadOnly);
    vector<string> fileObjectList = file.listObjectNames();
    regex imageGroupRegex("Image\\d+");
    auto numBands = count_if(fileObjectList.begin(), fileObjectList.end(), [imageGroupRegex](string s) { return regex_search(s, imageGroupRegex) > 0; });
    if (mpiRank==0)
        fmt::print("Opened file {} with {} slices\n", filename, numBands);
    int dims[]={0,0,0,0, 0, 0};
    while (true) {
        if (mpiRank==0) {
            fmt::print("Enter width, height, depth, x-, y-, and z-offsets of region: ");
            cin >> dims[0] >> dims[1] >> dims[2] >> dims[3] >> dims[4] >> dims[5];
            fmt::print("Stats for region {}x{}x{}, offset @{},{},{}:\n", dims[0], dims[1], dims[2], dims[3], dims[4], dims[5]);
        }
        MPI_Bcast(dims, 4, MPI_INT, 0, MPI_COMM_WORLD);

        calculateStats(mpiRank, mpiSize, file, dims[0], dims[1], dims[2], dims[3], dims[4], dims[5]);
    }


}

void calculateStats(int mpiRank, int mpiSize, const HighFive::File &file, int width, int height, int depth, int xOffset, int yOffset, int zOffset) {
    std::vector<std::vector<float>> dataVector;
    vector<float> payloadGather;

    float nodeMaxVal = ::std::numeric_limits<float>::lowest();
    float nodeMinVal = ::std::numeric_limits<float>::max();
    float nodeSumX = 0;
    float nodeSumX2 = 0;
    long nodeNumElements = 0;


    for (auto band = zOffset; band < zOffset+depth; band++) {
        if (band % mpiSize != mpiRank)
            continue;

        Group group = file.getGroup(fmt::format("Image{0:03d}/skyData", band));
        DataSet dataSet = group.getDataSet(fmt::format("ImageDataArray_SB{0:03d}", band));
        DataSpace dataSpace = dataSet.getSpace();
        auto dims = dataSpace.getDimensions();
        auto dimX = dims[0];
        auto dimY = dims[1];

        if (width == dimX && height ==dimY)
            dataSet.read(dataVector);
        else
            dataSet.select({xOffset, yOffset}, {width, height}).read(dataVector);

        float maxVal = ::std::numeric_limits<float>::lowest();
        float minVal = ::std::numeric_limits<float>::max();
        float sumX = 0;
        float sumX2 = 0;
        for (auto &row: dataVector) {
            for (auto &element: row) {
                maxVal = max(maxVal, element);
                minVal = min(minVal, element);
                sumX += element;
                sumX2 += element * element;
            }
        }
        long numElements = dimX * dimY;
        float muX = sumX / max(1L, numElements);
        nodeMaxVal = max(nodeMaxVal, maxVal);
        nodeMinVal = min(nodeMinVal, minVal);
        nodeSumX += sumX;
        nodeSumX2 += sumX2;
        nodeNumElements += numElements;
    }

    float payload[] = {nodeMinVal, nodeMaxVal, nodeSumX, nodeSumX2, nodeNumElements};
    int payloadElements = sizeof(payload) / sizeof(float);
    if (mpiRank == 0)
        payloadGather.resize(mpiSize * payloadElements, 0.0f);

    MPI_Gather(payload, sizeof(payload) / sizeof(float), MPI_FLOAT, payloadGather.data(), payloadElements, MPI_FLOAT, 0, MPI_COMM_WORLD);

    if (mpiRank == 0) {
        float globalMaxVal = ::std::numeric_limits<float>::lowest();
        float globalMinVal = ::std::numeric_limits<float>::max();
        float globalSumX = 0;
        float globalSumX2 = 0;
        float globalNumElements = 0;

        for (auto i = 0; i < mpiSize * payloadElements; i += payloadElements) {
            globalMinVal = min(globalMinVal, payloadGather[i]);
            globalMaxVal = max(globalMaxVal, payloadGather[i + 1]);
            globalSumX += payloadGather[i + 2];
            globalSumX2 += payloadGather[i + 3];
            globalNumElements += payloadGather[i + 4];
        }
        float globalMuX = globalSumX / max(1.0f, globalNumElements);
        float globalStdDev = sqrtf(globalSumX2 / max(1.0f, globalNumElements) - globalMuX * globalMuX);
        fmt::print("Global: Min: {}, Max: {}, Average: {}, StdDev: {}\n\n", globalMinVal, globalMaxVal, globalMuX, globalStdDev);
    }
}