#include <unistd.h>

#include <iomanip>
#include <iostream>

#include "../Catalog/VOTableController.h"

using namespace catalog;
using namespace std;

unique_ptr<Controller> _controller(nullptr);
string _root_folder = "/";

void TestOnFileListRequest();
void TestOnFileListRequest(CARTA::CatalogListRequest file_list_request);
void TestOnFileInfoRequest();
void TestOnFileInfoRequest(CARTA::CatalogFileInfoRequest file_info_request);
void TestOnOpenFileRequest();
void TestOnOpenFileRequest(CARTA::OpenCatalogFile open_file_request);
void TestOnFilterRequest();
void TestOnFilterRequest2();
void TestOnFilterRequest3();
void TestOnFilterRequest4();
void TestOnFilterRequest5();
void TestOnFilterRequest(CARTA::OpenCatalogFile open_file_request, CARTA::CatalogFilterRequest filter_request);

string GetCurrentWorkingPath();

int main(int argc, char* argv[]) {
    int test_case;
    cout << "Choose a test case:" << endl;
    cout << "    1) TestOnFileListRequest()" << endl;
    cout << "    2) TestOnFileInfoRequest()" << endl;
    cout << "    3) TestOnOpenFileRequest()" << endl;
    cout << "    4) TestOnFilterRequest()" << endl;
    cout << "    5) TestOnFilterRequest2()" << endl;
    cout << "    6) TestOnFilterRequest3()" << endl;
    cout << "    7) TestOnFilterRequest4()" << endl;
    cout << "    8) TestOnFilterRequest5()" << endl;
    cin >> test_case;

    switch (test_case) {
        case 1:
            TestOnFileListRequest();
            break;
        case 2:
            TestOnFileInfoRequest();
            break;
        case 3:
            TestOnOpenFileRequest();
            break;
        case 4:
            TestOnFilterRequest();
            break;
        case 5:
            TestOnFilterRequest2();
            break;
        case 6:
            TestOnFilterRequest3();
            break;
        case 7:
            TestOnFilterRequest4();
            break;
        case 8:
            TestOnFilterRequest5();
            break;
        default:
            cout << "No such test case!" << endl;
            break;
    }

    return 0;
}

// Test functions

void TestOnFileListRequest() {
    // Get the current working path and remove the "/" at the start of the path name
    string base_path = GetCurrentWorkingPath().replace(0, 1, "");

    CARTA::CatalogListRequest file_list_request;
    file_list_request.set_directory(base_path + "/images/votable");
    TestOnFileListRequest(file_list_request);

    CARTA::CatalogListRequest file_list_request2;
    file_list_request2.set_directory(base_path);
    TestOnFileListRequest(file_list_request2);

    CARTA::CatalogListRequest file_list_request3;
    file_list_request3.set_directory(base_path + "/images");
    TestOnFileListRequest(file_list_request3);
}

void TestOnFileListRequest(CARTA::CatalogListRequest file_list_request) {
    CARTA::CatalogListResponse file_list_response;
    cout << "Create an unique ptr for the Controller." << endl;

    string root_folder = GetCurrentWorkingPath();

    _controller = unique_ptr<Controller>(new Controller(root_folder));
    if (_controller) {
        _controller->OnFileListRequest(file_list_request, file_list_response);
    }
    Controller::Print(file_list_request);
    Controller::Print(file_list_response);
}

void TestOnFileInfoRequest() {
    CARTA::CatalogFileInfoRequest file_info_request;
    file_info_request.set_directory("$BASE/images/votable");
    file_info_request.set_name("simple.xml");
    TestOnFileInfoRequest(file_info_request);

    CARTA::CatalogFileInfoRequest file_info_request2;
    file_info_request2.set_directory("$BASE/images/votable");
    file_info_request2.set_name("M17_SWex_simbad_2arcmin.xml");
    TestOnFileInfoRequest(file_info_request2);

    CARTA::CatalogFileInfoRequest file_info_request3;
    file_info_request3.set_directory("$BASE/images/votable");
    file_info_request3.set_name("test.xml");
    TestOnFileInfoRequest(file_info_request3);

    CARTA::CatalogFileInfoRequest file_info_request4;
    file_info_request4.set_directory("$BASE/images/votable");
    file_info_request4.set_name("vizier_votable.vot");
    TestOnFileInfoRequest(file_info_request4);

    CARTA::CatalogFileInfoRequest file_info_request5;
    file_info_request5.set_directory("$BASE/images/votable");
    file_info_request5.set_name("vizier_votable_47115.vot");
    TestOnFileInfoRequest(file_info_request5);
}

void TestOnFileInfoRequest(CARTA::CatalogFileInfoRequest file_info_request) {
    CARTA::CatalogFileInfoResponse file_info_response;
    cout << "Create an unique ptr for the Controller." << endl;
    _controller = unique_ptr<Controller>(new Controller(_root_folder));
    if (_controller) {
        _controller->OnFileInfoRequest(file_info_request, file_info_response);
    }
    Controller::Print(file_info_request);
    Controller::Print(file_info_response);
}

void TestOnOpenFileRequest() {
    CARTA::OpenCatalogFile open_file_request;
    open_file_request.set_directory("$BASE/images/votable");
    open_file_request.set_name("simple.xml");
    open_file_request.set_file_id(0);
    open_file_request.set_preview_data_size(10);
    TestOnOpenFileRequest(open_file_request);

    CARTA::OpenCatalogFile open_file_request2;
    open_file_request2.set_directory("$BASE/images/votable");
    open_file_request2.set_name("M17_SWex_simbad_2arcmin.xml");
    open_file_request2.set_file_id(0);
    open_file_request2.set_preview_data_size(10);
    TestOnOpenFileRequest(open_file_request2);

    CARTA::OpenCatalogFile open_file_request3;
    open_file_request3.set_directory("$BASE/images/votable");
    open_file_request3.set_name("test.xml");
    open_file_request3.set_file_id(0);
    open_file_request3.set_preview_data_size(10);
    TestOnOpenFileRequest(open_file_request3);

    CARTA::OpenCatalogFile open_file_request4;
    open_file_request4.set_directory("$BASE/images/votable");
    open_file_request4.set_name("vizier_votable.vot");
    open_file_request4.set_file_id(0);
    open_file_request4.set_preview_data_size(10);
    TestOnOpenFileRequest(open_file_request4);
}

void TestOnOpenFileRequest(CARTA::OpenCatalogFile open_file_request) {
    // Open file
    CARTA::OpenCatalogFileAck open_file_response;
    cout << "Create an unique ptr for the Controller." << endl;
    _controller = unique_ptr<Controller>(new Controller(_root_folder));
    if (_controller) {
        _controller->OnOpenFileRequest(open_file_request, open_file_response);
    }

    // Close file
    CARTA::CloseCatalogFile close_file_request;
    close_file_request.set_file_id(open_file_request.file_id());
    if (_controller) {
        _controller->OnCloseFileRequest(close_file_request);
    }

    // Print results
    Controller::Print(open_file_request);
    Controller::Print(open_file_response);
    Controller::Print(close_file_request);

    // Delete the Controller
    cout << "Reset the unique ptr for the Controller." << endl;
    _controller.reset();
}

void TestOnFilterRequest() {
    CARTA::OpenCatalogFile open_file_request;
    open_file_request.set_directory("$BASE/images/votable");
    open_file_request.set_name("simple.xml");
    open_file_request.set_file_id(0);
    open_file_request.set_preview_data_size(0);

    CARTA::CatalogFilterRequest filter_request;
    filter_request.set_file_id(0);
    filter_request.set_subset_start_index(0);
    filter_request.set_subset_data_size(-1);
    filter_request.set_image_file_id(0);
    filter_request.set_region_id(0);

    auto catalog_image_bounds = filter_request.mutable_image_bounds();
    catalog_image_bounds->set_x_column_name("RA");
    catalog_image_bounds->set_y_column_name("Dec");
    auto image_bounds = catalog_image_bounds->mutable_image_bounds();
    image_bounds->set_x_min(0);
    image_bounds->set_x_max(10);
    image_bounds->set_y_min(0);
    image_bounds->set_y_max(10);

    filter_request.add_hided_headers("Name");
    filter_request.add_hided_headers("RVel");
    filter_request.add_hided_headers("e_RVel");
    filter_request.add_hided_headers("R");

    auto filter_config = filter_request.add_filter_configs();
    filter_config->set_column_name("RA");
    filter_config->set_comparison_operator(CARTA::ComparisonOperator::FromTo);
    filter_config->set_min(0);
    filter_config->set_max(100);

    TestOnFilterRequest(open_file_request, filter_request);
}

void TestOnFilterRequest2() {
    CARTA::OpenCatalogFile open_file_request;
    open_file_request.set_directory("$BASE/images/votable");
    open_file_request.set_name("M17_SWex_simbad_2arcmin.xml");
    open_file_request.set_file_id(0);
    open_file_request.set_preview_data_size(0);

    CARTA::CatalogFilterRequest filter_request;
    filter_request.set_file_id(0);
    filter_request.set_subset_start_index(0);
    filter_request.set_subset_data_size(10);
    filter_request.set_image_file_id(0);
    filter_request.set_region_id(0);

    auto catalog_image_bounds = filter_request.mutable_image_bounds();
    catalog_image_bounds->set_x_column_name("RA_d");
    catalog_image_bounds->set_y_column_name("DEC_d");
    auto image_bounds = catalog_image_bounds->mutable_image_bounds();
    image_bounds->set_x_min(0);
    image_bounds->set_x_max(100);
    image_bounds->set_y_min(0);
    image_bounds->set_y_max(100);

    filter_request.add_hided_headers("OID4");
    filter_request.add_hided_headers("XMM:Obsno");
    filter_request.add_hided_headers("IUE:bibcode");
    filter_request.add_hided_headers("IUE:F");
    filter_request.add_hided_headers("IUE:Comments");
    filter_request.add_hided_headers("IUE:S");
    filter_request.add_hided_headers("IUE:CEB");
    filter_request.add_hided_headers("IUE:m");
    filter_request.add_hided_headers("IUE:ExpTim");
    filter_request.add_hided_headers("IUE:Time");
    filter_request.add_hided_headers("IUE:ObsDate");
    filter_request.add_hided_headers("IUE:MD");
    filter_request.add_hided_headers("IUE:FES");
    filter_request.add_hided_headers("IUE:A");
    filter_request.add_hided_headers("IUE:IMAGE");

    auto filter_config = filter_request.add_filter_configs();
    filter_config->set_column_name("RA_d");
    filter_config->set_comparison_operator(CARTA::ComparisonOperator::GreaterThan);
    filter_config->set_min(275.089);
    filter_config->set_max(275.089);

    TestOnFilterRequest(open_file_request, filter_request);
}

void TestOnFilterRequest3() {
    CARTA::OpenCatalogFile open_file_request;
    open_file_request.set_directory("$BASE/images/votable");
    open_file_request.set_name("vizier_votable.vot");
    open_file_request.set_file_id(0);
    open_file_request.set_preview_data_size(0);

    CARTA::CatalogFilterRequest filter_request;
    filter_request.set_file_id(0);
    filter_request.set_subset_start_index(0);
    filter_request.set_subset_data_size(10);
    filter_request.set_image_file_id(0);
    filter_request.set_region_id(0);

    auto catalog_image_bounds = filter_request.mutable_image_bounds();
    catalog_image_bounds->set_x_column_name("RA");
    catalog_image_bounds->set_y_column_name("Dec");
    auto image_bounds = catalog_image_bounds->mutable_image_bounds();
    image_bounds->set_x_min(0);
    image_bounds->set_x_max(10);
    image_bounds->set_y_min(0);
    image_bounds->set_y_max(10);

    filter_request.add_hided_headers("MPCM");
    filter_request.add_hided_headers("CID");
    filter_request.add_hided_headers("R");
    filter_request.add_hided_headers("recno");

    auto filter_config = filter_request.add_filter_configs();
    filter_config->set_column_name("RAJ2000");
    filter_config->set_comparison_operator(CARTA::ComparisonOperator::FromTo);
    filter_config->set_min(0);
    filter_config->set_max(100);

    TestOnFilterRequest(open_file_request, filter_request);
}

void TestOnFilterRequest4() {
    CARTA::OpenCatalogFile open_file_request;
    open_file_request.set_directory("$BASE/images/votable");
    open_file_request.set_name("vizier_votable_47115.vot");
    open_file_request.set_file_id(0);
    open_file_request.set_preview_data_size(10);

    CARTA::CatalogFilterRequest filter_request;
    filter_request.set_file_id(0);
    filter_request.set_subset_start_index(0);
    filter_request.set_subset_data_size(100);
    filter_request.set_image_file_id(0);
    filter_request.set_region_id(0);

    filter_request.add_hided_headers("z");
    filter_request.add_hided_headers("Band");
    filter_request.add_hided_headers("e_Flux");
    filter_request.add_hided_headers("Freq");
    filter_request.add_hided_headers("Obs.date");
    filter_request.add_hided_headers("Name");
    filter_request.add_hided_headers("_DEJ2000");
    filter_request.add_hided_headers("_RAJ2000");

    auto filter_config = filter_request.add_filter_configs();
    filter_config->set_column_name("Flux");
    filter_config->set_comparison_operator(CARTA::ComparisonOperator::FromTo);
    filter_config->set_min(1.0);
    filter_config->set_max(2.0);

    TestOnFilterRequest(open_file_request, filter_request);
}

void TestOnFilterRequest5() {
    CARTA::OpenCatalogFile open_file_request;
    open_file_request.set_directory("$BASE/images/votable");
    open_file_request.set_name("2MRS.votable");
    open_file_request.set_file_id(0);
    open_file_request.set_preview_data_size(10);

    CARTA::CatalogFilterRequest filter_request;
    filter_request.set_file_id(0);
    filter_request.set_subset_start_index(0);
    filter_request.set_subset_data_size(100);
    filter_request.set_image_file_id(0);
    filter_request.set_region_id(0);

    filter_request.add_hided_headers("prx10");
    filter_request.add_hided_headers("prx5");
    filter_request.add_hided_headers("prx2");
    filter_request.add_hided_headers("Dm");
    filter_request.add_hided_headers("zspec");
    filter_request.add_hided_headers("Mstellar");
    filter_request.add_hided_headers("dMabs");
    filter_request.add_hided_headers("Kabs");
    filter_request.add_hided_headers("Kmag");
    filter_request.add_hided_headers("glat");
    filter_request.add_hided_headers("glon");
    filter_request.add_hided_headers("name");

    auto filter_config = filter_request.add_filter_configs();
    filter_config->set_column_name("Z");
    filter_config->set_comparison_operator(CARTA::ComparisonOperator::GreaterThan);
    filter_config->set_min(0.0);
    filter_config->set_max(0.0);

    auto filter_config2 = filter_request.add_filter_configs();
    filter_config2->set_column_name("Y");
    filter_config2->set_comparison_operator(CARTA::ComparisonOperator::GreaterThan);
    filter_config2->set_min(0.0);
    filter_config2->set_max(0.0);

    auto filter_config3 = filter_request.add_filter_configs();
    filter_config3->set_column_name("X");
    filter_config3->set_comparison_operator(CARTA::ComparisonOperator::GreaterThan);
    filter_config3->set_min(0.0);
    filter_config3->set_max(0.0);

    TestOnFilterRequest(open_file_request, filter_request);
}

void TestOnFilterRequest(CARTA::OpenCatalogFile open_file_request, CARTA::CatalogFilterRequest filter_request) {
    // Open file
    CARTA::OpenCatalogFileAck open_file_response;
    cout << "Create an unique ptr for the Controller." << endl;
    _controller = unique_ptr<Controller>(new Controller(_root_folder));
    if (_controller) {
        _controller->OnOpenFileRequest(open_file_request, open_file_response);
    }

    // Filter the file data
    if (_controller) {
        _controller->OnFilterRequest(filter_request, [&](CARTA::CatalogFilterResponse filter_response) {
            // Print partial or final results
            Controller::Print(filter_request);
            Controller::Print(filter_response);
            cout << "\n------------------------------------------------------------------\n";
        });
    }

    // Close file
    CARTA::CloseCatalogFile close_file_request;
    close_file_request.set_file_id(open_file_request.file_id());
    if (_controller) {
        _controller->OnCloseFileRequest(close_file_request);
    }

    // Delete the Controller
    cout << "Reset the unique ptr for the Controller." << endl;
    _controller.reset();
}

string GetCurrentWorkingPath() {
    char buff[FILENAME_MAX];
    getcwd(buff, FILENAME_MAX);
    std::string current_working_path(buff);
    return current_working_path;
}