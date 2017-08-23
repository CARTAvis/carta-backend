#include <iostream>
#include <vector>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <highfive/H5File.hpp>

using namespace std;
using namespace HighFive;

int main()
{
	File file("L13591_sky.h5", File::ReadOnly);
	Group group = file.getGroup("Image001/skyData");
	DataSet dataSet = group.getDataSet("ImageDataArray_SB001");
	DataSpace dataSpace = dataSet.getSpace();
	auto dims = dataSpace.getDimensions();
	fmt::print("Rank {} (", dims.size());
	size_t index = 0;
	for (auto dim: dims)
	{
		if (++index != dims.size())
			fmt::print("{} x ", dim);
		else
			fmt::print("{})\n", dim);
	}

	return 0;
}