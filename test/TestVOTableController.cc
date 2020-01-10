#include <iostream>

#include "../Catalog/VOTableController.h"

using namespace catalog;
using namespace std;

void TestOnFileListRequest();
void TestOnFileListRequest(CARTA::CatalogListRequest file_list_request);

void Print(CARTA::CatalogListRequest file_list_request);
void Print(CARTA::CatalogListResponse file_list_response);
void Print(CARTA::CatalogFileInfo file_info);

void TestMultiFiles();

int main(int argc, char* argv[]) {
    TestOnFileListRequest();

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
    cout << "description: " << file_info.description() << endl;
    cout << endl;
}