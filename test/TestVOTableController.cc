#include <iomanip>
#include <iostream>

#include "../Catalog/VOTableController.h"

using namespace catalog;
using namespace std;

unique_ptr<Controller> _controller(nullptr);

void TestOnFileListRequest();
void TestOnFileListRequest(CARTA::CatalogListRequest file_list_request);
void TestOnFileInfoRequest();
void TestOnFileInfoRequest(CARTA::CatalogFileInfoRequest file_info_request);
void TestOnOpenFileRequest();
void TestOnOpenFileRequest(CARTA::OpenCatalogFile open_file_request);
void TestOnFilterRequest();
void TestOnFilterRequest2();
void TestOnFilterRequest3();
void TestOnFilterRequest(CARTA::OpenCatalogFile open_file_request, CARTA::CatalogFilterRequest filter_request);

void Print(CARTA::CatalogListRequest file_list_request);
void Print(CARTA::CatalogListResponse file_list_response);
void Print(CARTA::CatalogFileInfo file_info);
void Print(CARTA::CatalogFileInfoRequest file_info_request);
void Print(CARTA::CatalogFileInfoResponse file_info_response);
void Print(CARTA::CatalogHeader header);
void Print(CARTA::OpenCatalogFile open_file_request);
void Print(CARTA::OpenCatalogFileAck open_file_response);
void Print(CARTA::CatalogColumnsData columns_data);
void Print(CARTA::CloseCatalogFile close_file_request);
void Print(CARTA::CatalogFilterRequest filter_request);
void Print(CARTA::FilterConfig filter_config);
void Print(CARTA::CatalogImageBounds catalog_image_bounds);
void Print(CARTA::CatalogFilterResponse filter_response);

string GetDataType(CARTA::EntryType data_type);
string GetBoolType(bool bool_type);
string GetFileType(CARTA::CatalogFileType file_type);
string GetComparisonOperator(CARTA::ComparisonOperator comparison_operator);

int main(int argc, char* argv[]) {
    int test_case;
    cout << "Choose a test case:" << endl;
    cout << "    1) TestOnFileListRequest()" << endl;
    cout << "    2) TestOnFileInfoRequest()" << endl;
    cout << "    3) TestOnOpenFileRequest()" << endl;
    cout << "    4) TestOnFilterRequest()" << endl;
    cout << "    5) TestOnFilterRequest2()" << endl;
    cout << "    6) TestOnFilterRequest3()" << endl;
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
        default:
            cout << "No such test case!" << endl;
            break;
    }

    return 0;
}

// Test functions

void TestOnFileListRequest() {
    CARTA::CatalogListRequest file_list_request;
    file_list_request.set_directory("$BASE/images");
    TestOnFileListRequest(file_list_request);

    CARTA::CatalogListRequest file_list_request2;
    file_list_request2.set_directory("$BASE");
    TestOnFileListRequest(file_list_request2);
}

void TestOnFileListRequest(CARTA::CatalogListRequest file_list_request) {
    CARTA::CatalogListResponse file_list_response;
    Controller::OnFileListRequest(file_list_request, file_list_response);
    Print(file_list_request);
    Print(file_list_response);
}

void TestOnFileInfoRequest() {
    CARTA::CatalogFileInfoRequest file_info_request;
    file_info_request.set_directory("$BASE/images");
    file_info_request.set_name("simple.xml");
    TestOnFileInfoRequest(file_info_request);

    CARTA::CatalogFileInfoRequest file_info_request2;
    file_info_request2.set_directory("$BASE/images");
    file_info_request2.set_name("M17_SWex_simbad_2arcmin.xml");
    TestOnFileInfoRequest(file_info_request2);

    CARTA::CatalogFileInfoRequest file_info_request3;
    file_info_request3.set_directory("$BASE/images");
    file_info_request3.set_name("test.xml");
    TestOnFileInfoRequest(file_info_request3);

    CARTA::CatalogFileInfoRequest file_info_request4;
    file_info_request4.set_directory("$BASE/images");
    file_info_request4.set_name("vizier_votable.vot");
    TestOnFileInfoRequest(file_info_request4);
}

void TestOnFileInfoRequest(CARTA::CatalogFileInfoRequest file_info_request) {
    CARTA::CatalogFileInfoResponse file_info_response;
    Controller::OnFileInfoRequest(file_info_request, file_info_response);
    Print(file_info_request);
    Print(file_info_response);
}

void TestOnOpenFileRequest() {
    CARTA::OpenCatalogFile open_file_request;
    open_file_request.set_directory("$BASE/images");
    open_file_request.set_name("simple.xml");
    open_file_request.set_file_id(0);
    open_file_request.set_preview_data_size(10);
    TestOnOpenFileRequest(open_file_request);

    CARTA::OpenCatalogFile open_file_request2;
    open_file_request2.set_directory("$BASE/images");
    open_file_request2.set_name("M17_SWex_simbad_2arcmin.xml");
    open_file_request2.set_file_id(0);
    open_file_request2.set_preview_data_size(10);
    TestOnOpenFileRequest(open_file_request2);

    CARTA::OpenCatalogFile open_file_request3;
    open_file_request3.set_directory("$BASE/images");
    open_file_request3.set_name("test.xml");
    open_file_request3.set_file_id(0);
    open_file_request3.set_preview_data_size(10);
    TestOnOpenFileRequest(open_file_request3);

    CARTA::OpenCatalogFile open_file_request4;
    open_file_request4.set_directory("$BASE/images");
    open_file_request4.set_name("vizier_votable.vot");
    open_file_request4.set_file_id(0);
    open_file_request4.set_preview_data_size(10);
    TestOnOpenFileRequest(open_file_request4);
}

void TestOnOpenFileRequest(CARTA::OpenCatalogFile open_file_request) {
    // Open file
    CARTA::OpenCatalogFileAck open_file_response;
    cout << "Create an unique ptr for the Controller." << endl;
    _controller = unique_ptr<Controller>(new Controller());
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
    Print(open_file_request);
    Print(open_file_response);
    Print(close_file_request);

    // Delete the Controller
    cout << "Reset the unique ptr for the Controller." << endl;
    _controller.reset();
}

void TestOnFilterRequest() {
    CARTA::OpenCatalogFile open_file_request;
    open_file_request.set_directory("$BASE/images");
    open_file_request.set_name("simple.xml");
    open_file_request.set_file_id(0);
    open_file_request.set_preview_data_size(0);

    CARTA::CatalogFilterRequest filter_request;
    filter_request.set_file_id(0);
    filter_request.set_subset_start_index(0);
    filter_request.set_subset_data_size(50);
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
    open_file_request.set_directory("$BASE/images");
    open_file_request.set_name("M17_SWex_simbad_2arcmin.xml");
    open_file_request.set_file_id(0);
    open_file_request.set_preview_data_size(0);

    CARTA::CatalogFilterRequest filter_request;
    filter_request.set_file_id(0);
    filter_request.set_subset_start_index(0);
    filter_request.set_subset_data_size(10);
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
    open_file_request.set_directory("$BASE/images");
    open_file_request.set_name("vizier_votable.vot");
    open_file_request.set_file_id(0);
    open_file_request.set_preview_data_size(0);

    CARTA::CatalogFilterRequest filter_request;
    filter_request.set_file_id(0);
    filter_request.set_subset_start_index(0);
    filter_request.set_subset_data_size(10);
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

void TestOnFilterRequest(CARTA::OpenCatalogFile open_file_request, CARTA::CatalogFilterRequest filter_request) {
    // Open file
    CARTA::OpenCatalogFileAck open_file_response;
    cout << "Create an unique ptr for the Controller." << endl;
    _controller = unique_ptr<Controller>(new Controller());
    if (_controller) {
        _controller->OnOpenFileRequest(open_file_request, open_file_response);
    }

    // Filter the file data
    if (_controller) {
        _controller->OnFilterRequest(filter_request, [&](CARTA::CatalogFilterResponse filter_response) {
            // Print partial or final results
            Print(filter_request);
            Print(filter_response);
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

// Print functions

void Print(CARTA::CatalogListRequest file_list_request) {
    cout << "CatalogListRequest:" << endl;
    cout << "directory: " << file_list_request.directory() << endl;
    cout << endl;
}

void Print(CARTA::CatalogListResponse file_list_response) {
    cout << "CatalogListResponse:" << endl;
    cout << "success:   " << GetBoolType(file_list_response.success()) << endl;
    cout << "message:   " << file_list_response.message() << endl;
    cout << "directory: " << file_list_response.directory() << endl;
    cout << "parent:    " << file_list_response.parent() << endl;
    for (int i = 0; i < file_list_response.files_size(); ++i) {
        cout << "files(" << i << "):" << endl;
        auto file = file_list_response.files(i);
        Print(file);
    }
    for (int i = 0; i < file_list_response.subdirectories_size(); ++i) {
        cout << "subdirectories(" << i << "): " << file_list_response.subdirectories(i) << endl;
    }
    cout << endl;
}

void Print(CARTA::CatalogFileInfo file_info) {
    cout << "name:        " << file_info.name() << endl;
    cout << "type:        " << GetFileType(file_info.type()) << endl;
    cout << "file_size:   " << file_info.file_size() << "(KB)" << endl;
    cout << "description: " << file_info.description() << endl;
    cout << endl;
}

void Print(CARTA::CatalogFileInfoRequest file_info_request) {
    cout << "CARTA::CatalogFileInfoRequest:" << endl;
    cout << "directory: " << file_info_request.directory() << endl;
    cout << "name:      " << file_info_request.name() << endl;
    cout << endl;
}

void Print(CARTA::CatalogFileInfoResponse file_info_response) {
    cout << "CARTA::CatalogFileInfoResponse:" << endl;
    cout << "success:   " << GetBoolType(file_info_response.success()) << endl;
    cout << "message:   " << file_info_response.message() << endl;
    cout << "file_info: " << endl;
    Print(file_info_response.file_info());
    cout << "data_size: " << file_info_response.data_size() << endl;
    for (int i = 0; i < file_info_response.headers_size(); ++i) {
        cout << "headers(" << i << "):" << endl;
        auto header = file_info_response.headers(i);
        Print(header);
    }
    cout << endl;
}

void Print(CARTA::CatalogHeader header) {
    cout << "CARTA::CatalogHeader:" << endl;
    cout << "name:            " << header.name() << endl;
    cout << "data_type:       " << GetDataType(header.data_type()) << endl;
    cout << "column_index:    " << header.column_index() << endl;
    cout << "data_type_index: " << header.data_type_index() << endl;
    cout << "description:     " << header.description() << endl;
    cout << "units:           " << header.units() << endl;
    cout << endl;
}

void Print(CARTA::OpenCatalogFile open_file_request) {
    cout << "CARTA::OpenCatalogFile:" << endl;
    cout << "directory:         " << open_file_request.directory() << endl;
    cout << "name:              " << open_file_request.name() << endl;
    cout << "file_id:           " << open_file_request.file_id() << endl;
    cout << "preview_data_size: " << open_file_request.preview_data_size() << endl;
    cout << endl;
}

void Print(CARTA::OpenCatalogFileAck open_file_response) {
    cout << "CARTA::OpenCatalogFileAck" << endl;
    cout << "success:   " << GetBoolType(open_file_response.success()) << endl;
    cout << "message:   " << open_file_response.message() << endl;
    cout << "file_id:   " << open_file_response.file_id() << endl;
    Print(open_file_response.file_info());
    cout << "data_size: " << open_file_response.data_size() << endl;
    for (int i = 0; i < open_file_response.headers_size(); ++i) {
        cout << "headers(" << i << "):" << endl;
        Print(open_file_response.headers(i));
    }
    Print(open_file_response.columns_data());
    cout << endl;
}

void Print(CARTA::CatalogColumnsData columns_data) {
    for (int i = 0; i < columns_data.bool_column_size(); ++i) {
        cout << "bool_columns(" << i << "):" << endl;
        auto column = columns_data.bool_column(i);
        for (int j = 0; j < column.bool_column_size(); ++j) {
            cout << column.bool_column(j) << " | ";
        }
        cout << endl;
    }
    for (int i = 0; i < columns_data.string_column_size(); ++i) {
        cout << "string_columns(" << i << "):" << endl;
        auto column = columns_data.string_column(i);
        for (int j = 0; j < column.string_column_size(); ++j) {
            cout << column.string_column(j) << " | ";
        }
        cout << endl;
    }
    for (int i = 0; i < columns_data.int_column_size(); ++i) {
        cout << "int_columns(" << i << "):" << endl;
        auto column = columns_data.int_column(i);
        for (int j = 0; j < column.int_column_size(); ++j) {
            cout << column.int_column(j) << " | ";
        }
        cout << endl;
    }
    for (int i = 0; i < columns_data.ll_column_size(); ++i) {
        cout << "ll_columns(" << i << "):" << endl;
        auto column = columns_data.ll_column(i);
        for (int j = 0; j < column.ll_column_size(); ++j) {
            cout << column.ll_column(j) << " | ";
        }
        cout << endl;
    }
    for (int i = 0; i < columns_data.float_column_size(); ++i) {
        cout << "float_columns(" << i << "):" << endl;
        auto column = columns_data.float_column(i);
        for (int j = 0; j < column.float_column_size(); ++j) {
            cout << std::setprecision(10) << column.float_column(j) << " | ";
        }
        cout << endl;
    }
    for (int i = 0; i < columns_data.double_column_size(); ++i) {
        cout << "double_columns(" << i << "):" << endl;
        auto column = columns_data.double_column(i);
        for (int j = 0; j < column.double_column_size(); ++j) {
            cout << std::setprecision(10) << column.double_column(j) << " | ";
        }
        cout << endl;
    }
}

void Print(CARTA::CloseCatalogFile close_file_request) {
    cout << "CARTA::CloseCatalogFile:" << endl;
    cout << "file_id: " << close_file_request.file_id() << endl;
    cout << endl;
}

void Print(CARTA::CatalogFilterRequest filter_request) {
    cout << "CARTA::CatalogFilterRequest:" << endl;
    cout << "file_id:           " << filter_request.file_id() << endl;
    cout << "hided_headers:     " << endl;
    for (int i = 0; i < filter_request.hided_headers_size(); ++i) {
        cout << filter_request.hided_headers(i) << " | ";
    }
    cout << endl;
    for (int i = 0; i < filter_request.filter_configs_size(); ++i) {
        cout << "filter_config(" << i << "):" << endl;
        auto filter = filter_request.filter_configs(i);
        Print(filter);
    }
    cout << "subset_data_size:   " << filter_request.subset_data_size() << endl;
    cout << "subset_start_index: " << filter_request.subset_start_index() << endl;
    Print(filter_request.image_bounds());
    cout << "region_id:          " << filter_request.region_id() << endl;
    cout << endl;
}

void Print(CARTA::FilterConfig filter_config) {
    cout << "CARTA::FilterConfig:" << endl;
    cout << "column_name:         " << filter_config.column_name() << endl;
    cout << "comparison_operator: " << GetComparisonOperator(filter_config.comparison_operator()) << endl;
    cout << "min:                 " << filter_config.min() << endl;
    cout << "max:                 " << filter_config.max() << endl;
    cout << "sub_string:          " << filter_config.sub_string() << endl;
    cout << endl;
}

void Print(CARTA::CatalogImageBounds catalog_image_bounds) {
    cout << "CARTA::CatalogImageBounds:" << endl;
    cout << "x_column_name: " << catalog_image_bounds.x_column_name() << endl;
    cout << "y_column_name: " << catalog_image_bounds.y_column_name() << endl;
    auto image_bounds = catalog_image_bounds.image_bounds();
    cout << "x_min: " << image_bounds.x_min() << endl;
    cout << "x_max: " << image_bounds.x_max() << endl;
    cout << "y_min: " << image_bounds.y_min() << endl;
    cout << "y_max: " << image_bounds.y_max() << endl;
    cout << endl;
}

void Print(CARTA::CatalogFilterResponse filter_response) {
    cout << "CARTA::CatalogFilterResponse:" << endl;
    cout << "file_id:   " << filter_response.file_id() << endl;
    cout << "region_id: " << filter_response.region_id() << endl;
    for (int i = 0; i < filter_response.headers_size(); ++i) {
        cout << "headers(" << i << "):" << endl;
        auto header = filter_response.headers(i);
        Print(header);
    }
    Print(filter_response.columns_data());
    cout << "progress:  " << filter_response.progress() << endl;
    cout << endl;
}

string GetDataType(CARTA::EntryType data_type) {
    string result;
    switch (data_type) {
        case CARTA::EntryType::BOOL:
            result = "bool";
            break;
        case CARTA::EntryType::STRING:
            result = "string";
            break;
        case CARTA::EntryType::INT:
            result = "int";
            break;
        case CARTA::EntryType::LONGLONG:
            result = "long long";
            break;
        case CARTA::EntryType::FLOAT:
            result = "float";
            break;
        case CARTA::EntryType::DOUBLE:
            result = "double";
            break;
        default:
            result = "unknown data type";
            break;
    }
    return result;
}

string GetBoolType(bool bool_type) {
    string result;
    if (bool_type) {
        result = "true";
    } else {
        result = "false";
    }
    return result;
}

string GetFileType(CARTA::CatalogFileType file_type) {
    string result;
    switch (file_type) {
        case CARTA::CatalogFileType::VOTable:
            result = "VOTable";
            break;
        default:
            result = "unknown Catalog file type";
            break;
    }
    return result;
}

string GetComparisonOperator(CARTA::ComparisonOperator comparison_operator) {
    string result;
    switch (comparison_operator) {
        case CARTA::ComparisonOperator::EqualTo:
            result = "==";
            break;
        case CARTA::ComparisonOperator::NotEqualTo:
            result = "!=";
            break;
        case CARTA::ComparisonOperator::LessThan:
            result = "<";
            break;
        case CARTA::ComparisonOperator::GreaterThan:
            result = ">";
            break;
        case CARTA::ComparisonOperator::LessThanOrEqualTo:
            result = "<=";
            break;
        case CARTA::ComparisonOperator::GreaterThanOrEqualTo:
            result = ">=";
            break;
        case CARTA::ComparisonOperator::BetweenAnd:
            result = "...";
            break;
        case CARTA::ComparisonOperator::FromTo:
            result = "..";
            break;
        default:
            result = "unknown comparison operator!";
            break;
    }
    return result;
}