#include <iostream>
#include <vector>
#include <algorithm>
#include <regex>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <highfive/H5File.hpp>

using namespace std;
using namespace HighFive;

int main()
{
	File file("/home/angus/Downloads/L13591_sky.h5", File::ReadOnly);

	vector<vector<float>> result;
	int subDiv=2;
	vector<string> fileObjectList = file.listObjectNames();
	regex imageGroupRegex("Image\\d+");
	auto numBands = std::count_if(fileObjectList.begin(), fileObjectList.end(), [imageGroupRegex](string s){return regex_search(s, imageGroupRegex)>0;});

	float globalMaxVal = numeric_limits<float>::lowest();
	float globalMinVal = numeric_limits<float>::max();
	float globalSumX = 0;
	float globalSumX2 = 0;
	long globalNumElements = 0;
	for (auto band = 0; band<numBands;band++)
	{
		Group group = file.getGroup(fmt::format("Image{0:03d}/skyData", band));
		DataSet dataSet = group.getDataSet(fmt::format("ImageDataArray_SB{0:03d}", band));
		DataSpace dataSpace = dataSet.getSpace();
		auto dims = dataSpace.getDimensions();
		auto dimX = dims[0]/subDiv;
		auto dimY = dims[1]/subDiv;

		for (auto i=0;i<subDiv;i++)
		{
			for (auto j=0;j<subDiv;j++)
			{
				if (subDiv>1)
					dataSet.select({i*dimX, j*dimY}, {dimX, dimY}).read(result);
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
						sumX2 += element*element;
					}
				}
				long numElements = dimX*dimY;
				float muX = sumX/max(1L, numElements);
				float stdDev = sqrtf(sumX2/max(1L, numElements)-muX*muX);
				globalMaxVal=max(globalMaxVal, maxVal);
				globalMinVal=min(globalMinVal, minVal);
				globalSumX+=sumX;
				globalSumX2+=sumX2;
				globalNumElements+=numElements;
			}

		}

		//fmt::print("Min: {}, Max: {}, Average: {}, StdDev: {}\n", minVal, maxVal, muX, stdDev);
	}

	float muX = globalSumX/max(1L, globalNumElements);
	float stdDev = sqrtf(globalSumX2/max(1L, globalNumElements)-muX*muX);
	fmt::print("Min: {}, Max: {}, Average: {}, StdDev: {}\n", globalMinVal, globalMaxVal, muX, stdDev);


	return 0;
}