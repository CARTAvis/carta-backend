#include <iostream>
#include <vector>
#include <algorithm>
#include <regex>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <highfive/H5File.hpp>

#include <mpi.h>

using namespace std;
using namespace HighFive;

int main(int argc, char** argv)
{
	int mpiRank, mpiSize;
	// initialize MPI
	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &mpiSize);
	MPI_Comm_rank(MPI_COMM_WORLD, &mpiRank);
	vector<float> payloadGather;
	//float* payloadGather = nullptr;

//	char processor_name[MPI_MAX_PROCESSOR_NAME];
//	int name_len;
//	MPI_Get_processor_name(processor_name, &name_len);

	File file("/home/angus/Downloads/L13591_sky.h5", File::ReadOnly);
	vector<vector<float>> result;
	int subDiv = 1;
	vector<string> fileObjectList = file.listObjectNames();
	regex imageGroupRegex("Image\\d+");
	auto numBands = std::count_if(fileObjectList.begin(), fileObjectList.end(), [imageGroupRegex](string s)
	{ return regex_search(s, imageGroupRegex) > 0; });

	float nodeMaxVal = numeric_limits<float>::lowest();
	float nodeMinVal = numeric_limits<float>::max();
	float nodeSumX = 0;
	float nodeSumX2 = 0;
	long nodeNumElements = 0;



	for (auto band = 0; band < numBands; band++)
	{
		if (band % mpiSize != mpiRank)
			continue;

		Group group = file.getGroup(fmt::format("Image{0:03d}/skyData", band));
		DataSet dataSet = group.getDataSet(fmt::format("ImageDataArray_SB{0:03d}", band));
		DataSpace dataSpace = dataSet.getSpace();
		auto dims = dataSpace.getDimensions();
		auto dimX = dims[0] / subDiv;
		auto dimY = dims[1] / subDiv;

		for (auto i = 0; i < subDiv; i++)
		{
			for (auto j = 0; j < subDiv; j++)
			{
				if (subDiv > 1)
					dataSet.select({i * dimX, j * dimY}, {dimX, dimY}).read(result);
				else
					dataSet.read(result);

				float maxVal = numeric_limits<float>::lowest();
				float minVal = numeric_limits<float>::max();
				float sumX = 0;
				float sumX2 = 0;
				for (auto& row: result)
				{
					for (auto& element: row)
					{
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

		}
	}
	//float muX = nodeSumX / max(1L, nodeNumElements);
	//float stdDev = sqrtf(nodeSumX2 / max(1L, nodeNumElements) - muX * muX);

	float payload[] = {nodeMinVal, nodeMaxVal, nodeSumX, nodeSumX2, nodeNumElements};
	int payloadElements = sizeof(payload)/sizeof(float);
	if (mpiRank == 0)
		payloadGather.resize(mpiSize * payloadElements, 0.0f);

	MPI_Gather(payload, sizeof(payload) / sizeof(float), MPI_FLOAT, payloadGather.data(), payloadElements, MPI_FLOAT, 0, MPI_COMM_WORLD);
	//fmt::print("Rank {}: Min: {}, Max: {}, Average: {}, StdDev: {}\n", mpiRank, nodeMinVal, nodeMaxVal, muX, stdDev);

	if (mpiRank==0)
	{
		float globalMaxVal = numeric_limits<float>::lowest();
		float globalMinVal = numeric_limits<float>::max();
		float globalSumX = 0;
		float globalSumX2 = 0;
		float globalNumElements = 0;

		for (auto i=0;i<mpiSize*payloadElements; i+=payloadElements)
		{
			globalMinVal = min(globalMinVal, payloadGather[i]);
			globalMaxVal = max(globalMaxVal, payloadGather[i+1]);
			globalSumX += payloadGather[i+2];
			globalSumX2 += payloadGather[i+3];
			globalNumElements += payloadGather[i+4];
		}
		float globalMuX = globalSumX / max(1.0f, globalNumElements);
		float globalStdDev = sqrtf(globalSumX2 / max(1.0f, globalNumElements) - globalMuX * globalMuX);
		fmt::print("Global: Min: {}, Max: {}, Average: {}, StdDev: {}\n", globalMinVal, globalMaxVal, globalMuX, globalStdDev);
	}
	MPI_Finalize();

	return 0;
}