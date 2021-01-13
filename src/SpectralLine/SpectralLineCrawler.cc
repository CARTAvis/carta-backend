/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "SpectralLineCrawler.h"

#include <curl/curl.h>

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

#include "../Table/Columns.h"

using namespace carta;

#define REST_FREQUENCY_COLUMN_INDEX 2
#define INTENSITY_LIMIT_WORKAROUND 0.000001
#define NUM_HEADERS 18

const std::string SpectralLineCrawler::Headers[] = {"Species", "Chemical Name", "Freq-MHz(rest frame,redshifted)",
    "Freq Err(rest frame,redshifted)", "Meas Freq-MHz(rest frame,redshifted)", "Meas Freq Err(rest frame,redshifted)", "Resolved QNs",
    "Unresolved Quantum Numbers", "CDMS/JPL Intensity", "S<sub>ij</sub>&#956;<sup>2</sup> (D<sup>2</sup>)", "S<sub>ij</sub>",
    "Log<sub>10</sub> (A<sub>ij</sub>)", "Lovas/AST Intensity", "E_L (cm^-1)", "E_L (K)", "E_U (cm^-1)", "E_U (K)", "Linelist"};

SpectralLineCrawler::SpectralLineCrawler() {}

SpectralLineCrawler::~SpectralLineCrawler() {}

/*
    References:
    1. curl example https://curl.haxx.se/libcurl/c/getinmemory.html
    2. c++ example https://gist.github.com/alghanmi/c5d7b761b2c9ab199157
*/
void SpectralLineCrawler::SendRequest(const CARTA::DoubleBounds& frequencyRange, const double line_intensity_lower_limit,
    CARTA::SpectralLineResponse& spectral_line_response) {
    /* init the curl session */
    CURL* curl_handle = curl_easy_init();
    if (curl_handle == nullptr) {
        spectral_line_response.set_success(false);
        spectral_line_response.set_message("Init curl failed.");
        return;
    }
    std::string readBuffer;
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, SpectralLineCrawler::WriteMemoryCallback);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

    /* specify URL to get */
    // TODO: assemble parameters when frontend offers split settings
#ifdef SPLATALOGUE_URL
    std::string splatalog_url = SPLATALOGUE_URL;
#else
    std::string splatalog_url = "https://splatalogue.online";
#endif
    std::string base = "/c_export.php?&sid%5B%5D=&data_version=v3.0&lill=on";
    std::string intensityLimit =
        std::isnan(line_intensity_lower_limit)
            ? ""
            : fmt::format("&lill_cdms_jpl={}", line_intensity_lower_limit == 0 ? INTENSITY_LIMIT_WORKAROUND : line_intensity_lower_limit);
    std::string lineListParameters =
        "&displayJPL=displayJPL&displayCDMS=displayCDMS&displayLovas=displayLovas"
        "&displaySLAIM=displaySLAIM&displayToyaMA=displayToyaMA&displayOSU=displayOSU"
        "&displayRecomb=displayRecomb&displayLisa=displayLisa&displayRFI=displayRFI";
    std::string lineStrengthParameters = "&ls1=ls1&ls2=ls2&ls3=ls3&ls4=ls4&ls5=ls5";
    std::string energyLevelParameters = "&el1=el1&el2=el2&el3=el3&el4=el4";
    std::string miscellaneousParameters =
        "&show_unres_qn=show_unres_qn&submit=Export&export_type=current&export_delimiter=tab"
        "&offset=0&limit=100000&range=on";
    double freqMin = frequencyRange.min();
    double freqMax = frequencyRange.max();
    // workaround to fix splatalogue frequency range parameter bug
    auto freqMinString = fmt::format(freqMin == std::floor(freqMin) ? "{:.0f}" : "{}", freqMin);
    auto freqMaxString = fmt::format(freqMax == std::floor(freqMax) ? "{:.0f}" : "{}", freqMax);
    std::string frequencyRangeStr = fmt::format("&frequency_units=MHz&from={}&to={}", freqMinString, freqMaxString);
    std::string URL = splatalog_url + base + intensityLimit + lineListParameters + lineStrengthParameters + energyLevelParameters +
                      miscellaneousParameters + frequencyRangeStr;
    curl_easy_setopt(curl_handle, CURLOPT_URL, URL.c_str());

    /* fetch data & parse */
    CURLcode res = curl_easy_perform(curl_handle);
    if (res == CURLE_OK) {
        SpectralLineCrawler::ParseQueryResult(readBuffer, spectral_line_response);
    } else {
        spectral_line_response.set_success(false);
        spectral_line_response.set_message(fmt::format("curl_easy_perform() failed: {}", curl_easy_strerror(res)));
    }

    /* cleanup curl stuff */
    curl_easy_cleanup(curl_handle);

    return;
}

size_t SpectralLineCrawler::WriteMemoryCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

void SpectralLineCrawler::ParseQueryResult(const std::string& results, CARTA::SpectralLineResponse& spectral_line_response) {
    std::vector<std::string> headers;
    std::vector<std::vector<std::string>> data_columns;
    int num_data_rows = 0;
    std::istringstream line_stream(results);
    std::string line, token;

    // Extracting header part: fill in [Species, Chemical Name, ...] & create empty data columns
    std::getline(line_stream, line, '\n');
    std::istringstream header_token_stream(line);
    while (std::getline(header_token_stream, token, '\t')) {
        headers.push_back(token);
        data_columns.push_back(std::vector<std::string>());
    }

    // Checking extracted header numbers & common headers, [0] = "Species", [1] = "Chemical Name"
    if (headers.size() != NUM_HEADERS || headers[0] != SpectralLineCrawler::Headers[0] || headers[1] != SpectralLineCrawler::Headers[1]) {
        spectral_line_response.set_success(false);
        spectral_line_response.set_message("Received incorrect headers from splatalogue.");
        return;
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
