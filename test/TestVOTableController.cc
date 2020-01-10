#include <iostream>

#include "../Catalog/VOTableController.h"

using namespace catalog;
using namespace std;

void TestOnFileListRequest();
void TestOnFileListRequest(CARTA::CatalogListRequest file_list_request);
void TestOnFileInfoRequest();
void TestOnFileInfoRequest(CARTA::CatalogFileInfoRequest file_info_request);

void Print(CARTA::CatalogListRequest file_list_request);
void Print(CARTA::CatalogListResponse file_list_response);
void Print(CARTA::CatalogFileInfo file_info);
void Print(CARTA::CatalogFileInfoRequest file_info_request);
void Print(CARTA::CatalogFileInfoResponse file_info_response);
void Print(CARTA::CatalogHeader header);

int main(int argc, char* argv[]) {
    // TestOnFileListRequest();
    TestOnFileInfoRequest();

    return 0;
}

void TestOnFileListRequest() {
    CARTA::CatalogListRequest file_list_request;
    file_list_request.set_directory("images");
    TestOnFileListRequest(file_list_request);

    CARTA::CatalogListRequest file_list_request2;
    file_list_request2.set_directory("$BASE/images");
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
    file_info_request.set_directory("images");
    file_info_request.set_name("simple.xml");
    TestOnFileInfoRequest(file_info_request);

    CARTA::CatalogFileInfoRequest file_info_request2;
    file_info_request2.set_directory("images");
    file_info_request2.set_name("M17_SWex_simbad_2arcmin.xml");
    TestOnFileInfoRequest(file_info_request2);
}

void TestOnFileInfoRequest(CARTA::CatalogFileInfoRequest file_info_request) {
    CARTA::CatalogFileInfoResponse file_info_response;
    Controller::OnFileInfoRequest(file_info_request, file_info_response);
    Print(file_info_request);
    Print(file_info_response);
}

void Print(CARTA::CatalogListRequest file_list_request) {
    cout << "CatalogListRequest:" << endl;
    cout << "directory: " << file_list_request.directory() << endl;
    cout << endl;
}

void Print(CARTA::CatalogListResponse file_list_response) {
    cout << "CatalogListResponse:" << endl;
    cout << "success:   " << file_list_response.success() << endl;
    cout << "message:   " << file_list_response.message() << endl;
    cout << "directory: " << file_list_response.directory() << endl;
    cout << "parent:    " << file_list_response.parent() << endl;
    cout << "files:" << endl;
    for (int i = 0; i < file_list_response.files_size(); ++i) {
        auto file = file_list_response.files(i);
        Print(file);
    }
    cout << "subdirectories:" << endl;
    for (int i = 0; i < file_list_response.subdirectories_size(); ++i) {
        cout << file_list_response.subdirectories(i) << endl;
    }
    cout << endl;
}

void Print(CARTA::CatalogFileInfo file_info) {
    cout << "name:        " << file_info.name() << endl;
    cout << "type:        " << file_info.type() << endl;
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
    cout << "success:   " << file_info_response.success() << endl;
    cout << "message:   " << file_info_response.message() << endl;
    cout << "file_info: " << endl;
    Print(file_info_response.file_info());
    cout << "data_size: " << file_info_response.data_size() << endl;
    cout << "headers:" << endl;
    for (int i = 0; i < file_info_response.headers_size(); ++i) {
        auto header = file_info_response.headers(i);
        Print(header);
    }
    cout << endl;
}

void Print(CARTA::CatalogHeader header) {
    cout << "CARTA::CatalogHeader:" << endl;
    cout << "name:            " << header.name() << endl;
    cout << "data_type:       " << header.data_type() << endl;
    cout << "column_index:    " << header.column_index() << endl;
    cout << "data_type_index: " << header.data_type_index() << endl;
    cout << "description:     " << header.description() << endl;
    cout << "units:           " << header.units() << endl;
    cout << endl;
}