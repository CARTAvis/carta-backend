#include <iostream>

#include "../Catalog/VOTableController.h"

using namespace catalog;
using namespace std;

void TestOnFileListRequest();
void TestOnFileListRequest(CARTA::CatalogListRequest file_list_request);
void TestOnFileInfoRequest();
void TestOnFileInfoRequest(CARTA::CatalogFileInfoRequest file_info_request);
void TestOnOpenFileRequest();
void TestOnOpenFileRequest(CARTA::OpenCatalogFile open_file_request);

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

int main(int argc, char* argv[]) {
    // TestOnFileListRequest();
    // TestOnFileInfoRequest();
    TestOnOpenFileRequest();

    return 0;
}

// Test functions

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

void TestOnOpenFileRequest() {
    CARTA::OpenCatalogFile open_file_request;
    open_file_request.set_directory("images");
    open_file_request.set_name("simple.xml");
    open_file_request.set_file_id(0);
    open_file_request.set_preview_data_size(10);
    TestOnOpenFileRequest(open_file_request);

    CARTA::OpenCatalogFile open_file_request2;
    open_file_request2.set_directory("images");
    open_file_request2.set_name("M17_SWex_simbad_2arcmin.xml");
    open_file_request2.set_file_id(0);
    open_file_request2.set_preview_data_size(10);
    TestOnOpenFileRequest(open_file_request2);
}

void TestOnOpenFileRequest(CARTA::OpenCatalogFile open_file_request) {
    // Open file
    CARTA::OpenCatalogFileAck open_file_response;
    Controller controller = Controller();
    controller.OnOpenFileRequest(open_file_request, open_file_response);

    // Close file
    CARTA::CloseCatalogFile close_file_request;
    close_file_request.set_file_id(open_file_request.file_id());
    controller.OnCloseFileRequest(close_file_request);

    // Print results
    Print(open_file_request);
    Print(open_file_response);
    Print(close_file_request);
}

// Print functions

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
    cout << "data_type:       " << header.data_type() << endl;
    cout << "column_index:    " << header.column_index() << endl;
    cout << "data_type_index: " << header.data_type_index() << endl;
    cout << "description:     " << header.description() << endl;
    cout << "units:           " << header.units() << endl;
    cout << endl;
}

void Print(CARTA::OpenCatalogFile open_file_request) {
    cout << "CARTA::OpenCatalogFile:" << endl;
    cout << "directory: " << open_file_request.directory() << endl;
    cout << "name: " << open_file_request.name() << endl;
    cout << "file_id: " << open_file_request.file_id() << endl;
    cout << "preview_data_size: " << open_file_request.preview_data_size() << endl;
    cout << endl;
}

void Print(CARTA::OpenCatalogFileAck open_file_response) {
    cout << "CARTA::OpenCatalogFileAck" << endl;
    cout << "success: " << open_file_response.success() << endl;
    cout << "message: " << open_file_response.message() << endl;
    cout << "file_id: " << open_file_response.file_id() << endl;
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
            cout << column.float_column(j) << " | ";
        }
        cout << endl;
    }
    for (int i = 0; i < columns_data.double_column_size(); ++i) {
        cout << "double_columns(" << i << "):" << endl;
        auto column = columns_data.double_column(i);
        for (int j = 0; j < column.double_column_size(); ++j) {
            cout << column.double_column(j) << " | ";
        }
        cout << endl;
    }
}

void Print(CARTA::CloseCatalogFile close_file_request) {
    cout << "CARTA::CloseCatalogFile:" << endl;
    cout << "file_id: " << close_file_request.file_id() << endl;
    cout << endl;
}