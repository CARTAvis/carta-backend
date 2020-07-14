#include "SpectralLineRequest.h"

#include <curl/curl.h>

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

#include "Table/Columns.h"

using namespace carta;

#define REST_FREQUENCY_COLUMN_INDEX 2

const std::string SpectralLineRequest::SplatalogueURL =
    "https://www.cv.nrao.edu/php/splat/"
    "c_export.php?&sid%5B%5D=&data_version=v3.0&lill=on&displayJPL=displayJPL&displayCDMS=displayCDMS&displayLovas=displayLovas&"
    "displaySLAIM=displaySLAIM&displayToyaMA=displayToyaMA&displayOSU=displayOSU&displayRecomb=displayRecomb&displayLisa=displayLisa&"
    "displayRFI=displayRFI&ls1=ls1&ls2=ls2&ls3=ls3&ls4=ls4&ls5=ls5&el1=el1&el2=el2&el3=el3&el4=el4&show_unres_qn=show_unres_qn&"
    "submit=Export&export_type=current&export_delimiter=tab&offset=0&limit=100000&range=on";

SpectralLineRequest::SpectralLineRequest() {}

SpectralLineRequest::~SpectralLineRequest() {}

/* Referenced from curl example https://curl.haxx.se/libcurl/c/getinmemory.html */
void SpectralLineRequest::SendRequest(const CARTA::DoubleBounds& frequencyRange, CARTA::SpectralLineResponse& spectral_line_response) {
    CURL* curl_handle;
    CURLcode res;

    struct MemoryStruct chunk;

    /* will be grown as needed by the realloc above */
    chunk.memory = (char*)malloc(1);
    chunk.size = 0;

    curl_global_init(CURL_GLOBAL_ALL);

    /* init the curl session */
    curl_handle = curl_easy_init();

    /* specify URL to get */
    std::string frequencyRangeStr =
        "&frequency_units=MHz&from=" + std::to_string(frequencyRange.min()) + "&to=" + std::to_string(frequencyRange.max());
    std::string URL = SpectralLineRequest::SplatalogueURL + frequencyRangeStr;
    curl_easy_setopt(curl_handle, CURLOPT_URL, URL.c_str());

    /* send all data to this function  */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, SpectralLineRequest::WriteMemoryCallback);

    /* we pass our 'chunk' struct to the callback function */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void*)&chunk);

    /* some servers don't like requests that are made without a user-agent
      field, so we provide one */
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

    /* get it! */
    res = curl_easy_perform(curl_handle);

    /* parsing fetched content */
    if (res == CURLE_OK) {
        SpectralLineRequest::ParsingQueryResult(chunk, spectral_line_response);
    } else {
        spectral_line_response.set_success(false);
        spectral_line_response.set_message(fmt::format("curl_easy_perform() failed: {}", curl_easy_strerror(res)));
    }

    /* cleanup curl stuff */
    curl_easy_cleanup(curl_handle);
    free(chunk.memory);

    /* we're done with libcurl, so clean it up */
    curl_global_cleanup();

    return;
}

size_t SpectralLineRequest::WriteMemoryCallback(void* contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct* mem = (struct MemoryStruct*)userp;

    char* ptr = (char*)realloc(mem->memory, mem->size + realsize + 1);
    if (ptr == NULL) {
        /* out of memory! */
        std::cout << "not enough memory (realloc returned NULL)\n";
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

void SpectralLineRequest::ParsingQueryResult(const MemoryStruct& results, CARTA::SpectralLineResponse& spectral_line_response) {
    std::vector<std::string> headers;
    std::vector<std::vector<std::string>> data_columns;
    int num_data_rows = 0;
    std::istringstream line_stream(results.memory);
    std::string line, token;

    // Parsing header part: fill in [Species, Chemical Name, ...] & create empty data columns
    std::getline(line_stream, line, '\n');
    std::istringstream header_token_stream(line);
    while (std::getline(header_token_stream, token, '\t')) {
        headers.push_back(token);
        data_columns.push_back(std::vector<std::string>());
    }

    // Parsing data part: parsing each line & fill in data columns
    while (std::getline(line_stream, line, '\n')) {
        std::istringstream data_token_stream(line);
        int column_index = 0;
        while (std::getline(data_token_stream, token, '\t')) {
            if (column_index < headers.size()) {
                (data_columns[column_index]).push_back(token);
            }
            column_index++;
        }
        num_data_rows++;
    }

    // Fill in response's headers & column data,
    // insert additional rest frequency in index REST_FREQUENCY_COLUMN_INDEX
    auto response_headers = spectral_line_response.mutable_headers();
    auto response_columns = spectral_line_response.mutable_spectral_line_data();
    for (auto column_index = 0; column_index < headers.size() + 1; column_index++) {
        std::string column_name;
        std::unique_ptr<Column> column;
        if (column_index < REST_FREQUENCY_COLUMN_INDEX) {
            column_name = headers[column_index];
            column = Column::FromValues(data_columns[column_index], column_name);
        } else if (column_index == REST_FREQUENCY_COLUMN_INDEX) { // insert shifted frequency column
            column_name = "Shifted Frequency";
            column = Column::FromValues(data_columns[column_index], column_name);
        } else {
            column_name = headers[column_index - 1];
            column = Column::FromValues(data_columns[column_index - 1], column_name);
        }

        // headers
        auto response_header = response_headers->Add();
        response_header->set_name(column_name);
        response_header->set_column_index(column_index);

        // columns
        auto carta_column = CARTA::ColumnData();
        carta_column.set_data_type(CARTA::String);
        if (column) {
            column->FillColumnData(carta_column, false, IndexList(), 0, num_data_rows);
        }
        (*response_columns)[column_index] = carta_column;
    }

    spectral_line_response.set_data_size(num_data_rows);
    spectral_line_response.set_success(true);

    return;
}
