#include <iostream>

#include "../Catalog/VOTableCarrier.h"
#include "../Catalog/VOTableParser.h"

using namespace catalog;

void TestScanVOTable(std::string filename);

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <VOTable_file>" << std::endl;
        return 1;
    }

    std::string filename = argv[1];

    /*
     * this initialize the library and check potential ABI mismatches
     * between the version it was compiled for and the actual shared
     * library used.
     */
    LIBXML_TEST_VERSION

    TestScanVOTable(filename);

    return 0;
}

void TestScanVOTable(std::string filename) {
    if (!VOTableParser::IsVOTable(filename)) {
        std::cout << "File: " << filename << " is NOT a VOTable!" << std::endl;
        return;
    }

    VOTableCarrier* carrier = new VOTableCarrier();

    auto t_start = std::chrono::high_resolution_clock::now();
    VOTableParser parser(filename, carrier, false, true);
    auto t_end = std::chrono::high_resolution_clock::now();
    auto dt = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    carrier->PrintData();

    std::cout << "Time spending for the parser: " << dt << "(ms)" << std::endl;

    delete carrier;
}