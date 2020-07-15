#include "SpectralLineRequest.h"

#include <curl/curl.h>

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

#include "Table/Columns.h"

using namespace carta;

#define REST_FREQUENCY_COLUMN_INDEX 2

const std::string SpectralLineRequest::SplatalogueURLBase =
    "https://www.cv.nrao.edu/php/splat/c_export.php?&sid%5B%5D=&data_version=v3.0&lill=on";

SpectralLineRequest::SpectralLineRequest() {}

SpectralLineRequest::~SpectralLineRequest() {}

/*
    References:
    1. curl example https://curl.haxx.se/libcurl/c/getinmemory.html
    2. c++ example https://gist.github.com/alghanmi/c5d7b761b2c9ab199157
*/
void SpectralLineRequest::SendRequest(const CARTA::DoubleBounds& frequencyRange, CARTA::SpectralLineResponse& spectral_line_response) {
    /* init the curl session */
    CURL* curl_handle = curl_easy_init();
    if (!curl_handle) {
        spectral_line_response.set_success(false);
        spectral_line_response.set_message("Init curl failed.");
        return;
    }
    std::string readBuffer;
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, SpectralLineRequest::WriteMemoryCallback);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

    /* specify URL to get */
    // TODO: assemble parameters when frontend offers split settings
    std::string lineListParameters =
        "&displayJPL=displayJPL&displayCDMS=displayCDMS&displayLovas=displayLovas"
        "&displaySLAIM=displaySLAIM&displayToyaMA=displayToyaMA&displayOSU=displayOSU"
        "&displayRecomb=displayRecomb&displayLisa=displayLisa&displayRFI=displayRFI";
    std::string lineStrengthParameters = "&ls1=ls1&ls2=ls2&ls3=ls3&ls4=ls4&ls5=ls5";
    std::string energyLevelParameters = "&el1=el1&el2=el2&el3=el3&el4=el4";
    std::string miscellaneousParameters =
        "&show_unres_qn=show_unres_qn&submit=Export&export_type=current&export_delimiter=tab"
        "&offset=0&limit=100000&range=on";
    std::string frequencyRangeStr =
        fmt::format("&frequency_units=MHz&from={}&to={}", std::to_string(frequencyRange.min()), std::to_string(frequencyRange.max()));
    std::string URL = SpectralLineRequest::SplatalogueURLBase + lineListParameters + lineStrengthParameters + energyLevelParameters +
                      miscellaneousParameters + frequencyRangeStr;
    curl_easy_setopt(curl_handle, CURLOPT_URL, URL.c_str());

    /* fetch data & parse */
    CURLcode res = curl_easy_perform(curl_handle);
    if (res == CURLE_OK) {
        SpectralLineRequest::ParseQueryResult(readBuffer, spectral_line_response);
    } else {
        spectral_line_response.set_success(false);
        spectral_line_response.set_message(fmt::format("curl_easy_perform() failed: {}", curl_easy_strerror(res)));
    }

    /* cleanup curl stuff */
    curl_easy_cleanup(curl_handle);

    return;
}

size_t SpectralLineRequest::WriteMemoryCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

void SpectralLineRequest::ParseQueryResult(const std::string& results, CARTA::SpectralLineResponse& spectral_line_response) {
    std::vector<std::string> headers;
    std::vector<std::vector<std::string>> data_columns;
    int num_data_rows = 0;
    std::istringstream line_stream(results);
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
