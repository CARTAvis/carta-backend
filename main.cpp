#include <iostream>
#include <hdf5.h>

using namespace std;

int main()
{
	cout << "Hello, World!" << std::endl;
	auto file_id = H5Fcreate("file.h5", H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
	auto status = H5Fclose(file_id);
	cout<<status<<endl;
	return 0;
}