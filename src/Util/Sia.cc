/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Sia.h"

#include <curl/curl.h>
#include <spdlog/fmt/fmt.h>
#include <pugixml.hpp>

#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <fstream>
#include <mutex>
#include <set>

#include "FileSystem.h"
#include "Logger/Logger.h"
#include "String.h"
#include "Table/Table.h"

namespace carta {

namespace {

constexpr int64_t MAX_VOTABLE_BYTES = 64 * 1024 * 1024;
constexpr long FETCH_TIMEOUT_SECONDS = 60;
const std::string TABLEDATA_RESPONSE_FORMAT = "votable/td";

const std::set<std::string> ALLOWED_FILTER_KEYS = {"BAND", "TIME", "POL", "FOV", "SPATRES", "EXPTIME", "TIMERES", "SPECRP", "ID",
    "COLLECTION", "FACILITY", "INSTRUMENT", "DPTYPE", "CALIB", "TARGET", "FORMAT", "MAXREC"};

bool HasControlOrNonAsciiCharacter(const std::string& value) {
    return std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 0x20 || c >= 0x7f; });
}

std::string HostFromUrl(const std::string& url) {
    auto scheme_end = url.find("://");
    auto host_start = scheme_end == std::string::npos ? 0 : scheme_end + 3;
    auto host_end = url.find_first_of("/?#", host_start);
    return url.substr(host_start, host_end == std::string::npos ? std::string::npos : host_end - host_start);
}

size_t TextBodyCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
    auto* body = static_cast<std::string*>(userdata);
    if (body->size() + size * nitems > MAX_VOTABLE_BYTES) {
        return 0;
    }
    body->append(buffer, size * nitems);
    return size * nitems;
}

// The service reports errors as <INFO name="QUERY_STATUS" value="ERROR">message</INFO>
std::string ExtractVotableError(const std::string& body) {
    pugi::xml_document doc;
    if (!doc.load_string(body.c_str())) {
        return "";
    }
    for (const auto& info : doc.select_nodes("//INFO")) {
        auto node = info.node();
        std::string name = node.attribute("name").as_string();
        std::string value = node.attribute("value").as_string();
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::toupper(c); });
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::toupper(c); });
        if (name == "QUERY_STATUS" && value == "ERROR") {
            std::string message = node.text().as_string();
            return message.empty() ? "SIA service reported an error" : message;
        }
    }
    return "";
}

bool IsBinarySerialization(const std::string& body) {
    return body.find("<TABLEDATA") == std::string::npos &&
           (body.find("<BINARY") != std::string::npos || body.find("<FITS") != std::string::npos);
}

// Writes the response to a temporary file so it can be parsed with the catalog Table reader
class ScopedTempFile {
public:
    ScopedTempFile(const std::string& content, const std::string& extension) {
        auto pattern = (fs::temp_directory_path() / fmt::format("carta_sia_{}_XXXXXX{}", getpid(), extension)).string();
        std::vector<char> path_buffer(pattern.begin(), pattern.end());
        path_buffer.push_back('\0');
        // mkstemps creates the file exclusively: a symlink planted at a guessable path is not followed
        int fd = mkstemps(path_buffer.data(), static_cast<int>(extension.size()));
        if (fd < 0) {
            return;
        }
        _path = std::string(path_buffer.data());
        FILE* file = fdopen(fd, "wb");
        if (file) {
            fwrite(content.data(), 1, content.size(), file);
            fclose(file);
        } else {
            close(fd);
        }
    }
    ~ScopedTempFile() {
        std::error_code error_code;
        fs::remove(_path, error_code);
    }
    std::string Path() const {
        return _path.string();
    }

private:
    fs::path _path;
};

template <typename T>
bool NumericEntry(const Column* column, size_t row, double& value) {
    auto data_column = dynamic_cast<const DataColumn<T>*>(column);
    if (data_column && row < data_column->entries.size()) {
        value = static_cast<double>(data_column->entries[row]);
        return true;
    }
    return false;
}

double NumberAt(const Column* column, size_t row) {
    double value(std::numeric_limits<double>::quiet_NaN());
    if (!column) {
        return value;
    }
    NumericEntry<double>(column, row, value) || NumericEntry<float>(column, row, value) || NumericEntry<int64_t>(column, row, value) ||
        NumericEntry<int32_t>(column, row, value) || NumericEntry<int16_t>(column, row, value) || NumericEntry<uint8_t>(column, row, value);
    return value;
}

std::string StringAt(const Column* column, size_t row) {
    if (!column) {
        return "";
    }
    auto string_column = dynamic_cast<const DataColumn<std::string>*>(column);
    if (string_column && row < string_column->entries.size()) {
        return string_column->entries[row];
    }
    auto value = NumberAt(column, row);
    return std::isnan(value) ? "" : fmt::format("{}", value);
}

const Column* FindColumn(const Table& table, const std::string& name, const std::string& ucd = "") {
    auto column = table.GetColumnByName(name);
    if (column) {
        return column;
    }
    if (!ucd.empty()) {
        for (size_t i = 0; i < table.NumColumns(); i++) {
            auto candidate = table[i];
            if (candidate) {
                std::string candidate_ucd(candidate->ucd);
                std::transform(
                    candidate_ucd.begin(), candidate_ucd.end(), candidate_ucd.begin(), [](unsigned char c) { return std::tolower(c); });
                // A UCD may carry qualifiers, e.g. "pos.eq.ra;meta.main" for "pos.eq.ra"
                if (candidate_ucd == ucd || candidate_ucd.rfind(ucd + ";", 0) == 0) {
                    return candidate;
                }
            }
        }
    }
    return nullptr;
}

bool FetchVotable(const std::string& url, const TextFetcher& fetch_text, std::string& body, std::string& error_message) {
    HttpTextResult result;
    if (!fetch_text(url, result)) {
        error_message = fmt::format("SIA request failed: {}", result.error);
        return false;
    }

    // Error responses are often VOTables with a QUERY_STATUS INFO and a 4xx status
    auto service_error = ExtractVotableError(result.body);
    if (!service_error.empty()) {
        error_message = service_error;
        return false;
    }
    if (result.status < 200 || result.status >= 300) {
        error_message = fmt::format("SIA request returned HTTP status {}", result.status);
        return false;
    }

    body = std::move(result.body);
    const std::string utf8_bom("\xEF\xBB\xBF");
    if (body.rfind(utf8_bom, 0) == 0) {
        body.erase(0, utf8_bom.size());
    }
    body.erase(0, body.find_first_not_of(" \t\r\n"));
    return true;
}

std::string WithResponseFormat(const std::string& query_url) {
    auto separator = query_url.find('?') == std::string::npos ? '?' : '&';
    return fmt::format("{}{}RESPONSEFORMAT={}", query_url, separator, SafeStringEscape(TABLEDATA_RESPONSE_FORMAT));
}

} // namespace

std::vector<SiaService> ParseSiaServices(const std::vector<std::string>& entries) {
    std::vector<SiaService> services;
    for (const auto& entry : entries) {
        auto separator = entry.find('|');
        std::string name = separator == std::string::npos ? "" : entry.substr(0, separator);
        std::string url = separator == std::string::npos ? entry : entry.substr(separator + 1);
        if (url.empty()) {
            continue;
        }
        if (name.empty()) {
            name = HostFromUrl(url);
        }
        services.push_back({name, url});
    }
    return services;
}

bool BuildSiaQueryUrl(const CARTA::SiaQueryRequest& request, std::string& url, std::string& error_message) {
    if (!std::isfinite(request.ra()) || !std::isfinite(request.dec())) {
        error_message = "search centre coordinates are required";
        return false;
    }
    if (!std::isfinite(request.radius()) || request.radius() <= 0) {
        error_message = "a positive search radius is required";
        return false;
    }

    auto separator = request.service_url().find('?') == std::string::npos ? '?' : '&';
    url = fmt::format("{}{}POS={}", request.service_url(), separator,
        SafeStringEscape(fmt::format("CIRCLE {} {} {}", request.ra(), request.dec(), request.radius())));

    for (const auto& filter : request.filters()) {
        std::string key(filter.key());
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::toupper(c); });
        if (ALLOWED_FILTER_KEYS.find(key) == ALLOWED_FILTER_KEYS.end()) {
            error_message = fmt::format("{} is not a supported SIA query parameter", filter.key());
            return false;
        }
        if (filter.value().empty() || HasControlOrNonAsciiCharacter(filter.value())) {
            error_message = fmt::format("invalid value for SIA query parameter {}", key);
            return false;
        }
        url += fmt::format("&{}={}", key, SafeStringEscape(filter.value()));
    }
    return true;
}

bool CurlFetchText(const std::string& url, HttpTextResult& result) {
    // libcurl is also used by cfitsio; global init is reference-counted and safe to repeat
    static std::once_flag curl_init_flag;
    std::call_once(curl_init_flag, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });

    CURL* curl = curl_easy_init();
    if (!curl) {
        result.error = "failed to initialise curl";
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, FETCH_TIMEOUT_SECONDS);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "carta-backend");
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, TextBodyCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result.body);

    auto curl_result = curl_easy_perform(curl);
    if (curl_result != CURLE_OK) {
        result.error = curl_result == CURLE_WRITE_ERROR ? "response too large" : curl_easy_strerror(curl_result);
        curl_easy_cleanup(curl);
        return false;
    }
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status);
    curl_easy_cleanup(curl);
    return true;
}

bool IsDataLinkFormat(const std::string& access_format) {
    std::string format(access_format);
    std::transform(format.begin(), format.end(), format.begin(), [](unsigned char c) { return std::tolower(c); });
    return format.find("datalink") != std::string::npos;
}

// Services list every product of an observation, so results include files which are not images
static bool IsOpenableFormat(const std::string& access_format) {
    std::string format(access_format);
    std::transform(format.begin(), format.end(), format.begin(), [](unsigned char c) { return std::tolower(c); });
    return format.empty() || format.find("fits") != std::string::npos || IsDataLinkFormat(format);
}

bool RunSiaQuery(
    const std::string& query_url, std::vector<CARTA::SiaRecord>& records, std::string& error_message, const TextFetcher& fetch_text) {
    std::string body;
    if (!FetchVotable(query_url, fetch_text, body, error_message)) {
        return false;
    }

    if (IsBinarySerialization(body)) {
        // e.g. DaCHS defaults to BINARY; ask for TABLEDATA explicitly
        if (!FetchVotable(WithResponseFormat(query_url), fetch_text, body, error_message)) {
            return false;
        }
        if (IsBinarySerialization(body)) {
            error_message = "SIA service returned an unsupported VOTable serialization";
            return false;
        }
    }

    ScopedTempFile temp_file(body, ".xml");
    Table table(temp_file.Path());
    if (!table.IsValid()) {
        // A table-less VOTable with only fields is a valid empty result
        if (body.find("<VOTABLE") != std::string::npos && body.find("<TABLE") != std::string::npos &&
            body.find("<DATA") == std::string::npos) {
            return true;
        }
        error_message = fmt::format("could not parse SIA response: {}", table.ParseError());
        return false;
    }

    auto obs_id = FindColumn(table, "obs_id");
    auto target_name = FindColumn(table, "target_name", "meta.id;src");
    auto collection = FindColumn(table, "obs_collection");
    auto facility = FindColumn(table, "facility_name");
    auto instrument = FindColumn(table, "instrument_name");
    auto dataproduct_type = FindColumn(table, "dataproduct_type");
    auto calib_level = FindColumn(table, "calib_level");
    auto ra = FindColumn(table, "s_ra", "pos.eq.ra");
    auto dec = FindColumn(table, "s_dec", "pos.eq.dec");
    auto fov = FindColumn(table, "s_fov");
    auto em_min = FindColumn(table, "em_min");
    auto em_max = FindColumn(table, "em_max");
    auto t_min = FindColumn(table, "t_min");
    auto t_max = FindColumn(table, "t_max");
    auto t_exptime = FindColumn(table, "t_exptime");
    auto obs_title = FindColumn(table, "obs_title");
    auto proposal_title = FindColumn(table, "proposal_title");
    auto dataproduct_subtype = FindColumn(table, "dataproduct_subtype");
    // Services name the filter column differently; neither name is in ObsCore itself
    auto bandpass = FindColumn(table, "energy_bandpassname");
    if (!bandpass) {
        bandpass = FindColumn(table, "bandpass_name");
    }
    auto access_url = FindColumn(table, "access_url", "meta.ref.url");
    auto access_format = FindColumn(table, "access_format", "meta.code.mime");
    auto access_estsize = FindColumn(table, "access_estsize");

    for (size_t row = 0; row < table.NumRows(); row++) {
        // Services list every product of an observation, including documentation and other non-images
        if (!IsOpenableFormat(StringAt(access_format, row))) {
            continue;
        }

        CARTA::SiaRecord record;
        record.set_index(records.size() + 1);
        record.set_obs_id(StringAt(obs_id, row));
        record.set_target_name(StringAt(target_name, row));
        record.set_collection(StringAt(collection, row));
        record.set_facility(StringAt(facility, row));
        record.set_instrument(StringAt(instrument, row));
        record.set_dataproduct_type(StringAt(dataproduct_type, row));
        auto calib = NumberAt(calib_level, row);
        record.set_calib_level(std::isnan(calib) ? -1 : static_cast<int32_t>(calib));
        record.set_ra(NumberAt(ra, row));
        record.set_dec(NumberAt(dec, row));
        record.set_fov(NumberAt(fov, row));
        record.set_em_min(NumberAt(em_min, row));
        record.set_em_max(NumberAt(em_max, row));
        record.set_t_min(NumberAt(t_min, row));
        record.set_t_max(NumberAt(t_max, row));
        record.set_t_exptime(NumberAt(t_exptime, row));
        record.set_obs_title(StringAt(obs_title, row));
        record.set_proposal_title(StringAt(proposal_title, row));
        record.set_dataproduct_subtype(StringAt(dataproduct_subtype, row));
        record.set_bandpass(StringAt(bandpass, row));
        record.set_access_url(StringAt(access_url, row));
        record.set_access_format(StringAt(access_format, row));
        auto estsize = NumberAt(access_estsize, row);
        record.set_access_estsize(std::isnan(estsize) || estsize <= 0 ? -1 : static_cast<int64_t>(estsize));

        records.push_back(std::move(record));
    }
    return true;
}

bool ResolveDataLinkFileUrl(
    const std::string& links_url, std::string& file_url, std::string& error_message, const TextFetcher& fetch_text) {
    std::string body;
    if (!FetchVotable(links_url, fetch_text, body, error_message)) {
        return false;
    }
    if (IsBinarySerialization(body)) {
        if (!FetchVotable(WithResponseFormat(links_url), fetch_text, body, error_message)) {
            return false;
        }
    }

    ScopedTempFile temp_file(body, ".xml");
    Table table(temp_file.Path());
    if (!table.IsValid()) {
        error_message = fmt::format("could not parse DataLink response: {}", table.ParseError());
        return false;
    }

    auto access_url = FindColumn(table, "access_url");
    auto semantics = FindColumn(table, "semantics");
    auto content_type = FindColumn(table, "content_type");
    auto link_error = FindColumn(table, "error_message");

    for (size_t row = 0; row < table.NumRows(); row++) {
        auto url = StringAt(access_url, row);
        auto type = StringAt(content_type, row);
        std::transform(type.begin(), type.end(), type.begin(), [](unsigned char c) { return std::tolower(c); });
        if (StringAt(semantics, row) == "#this" && !url.empty() && StringAt(link_error, row).empty() &&
            (type.empty() || type.find("fits") != std::string::npos)) {
            file_url = url;
            return true;
        }
    }
    error_message = "DataLink document contains no loadable FITS file";
    return false;
}

bool ResolveSiaFileUrl(const std::string& access_url, bool is_datalink, int64_t max_size_bytes, std::string& file_url,
    std::string& error_message, const TextFetcher& fetch_text, const SizeProbeFetcher& probe_size) {
    std::string url(access_url);
    if (!ValidateRemoteFetchUrl(url, error_message)) {
        return false;
    }
    if (is_datalink) {
        std::string resolved;
        if (!ResolveDataLinkFileUrl(url, resolved, error_message, fetch_text) || !ValidateRemoteFetchUrl(resolved, error_message)) {
            return false;
        }
        url = resolved;
    }

    // An access URL may itself redirect to a DataLink document (e.g. GAVO getproduct URLs)
    const int max_indirections(2);
    for (int hop = 0; hop < max_indirections; ++hop) {
        std::string final_url, content_type;
        if (!CheckRemoteFileSize(url, max_size_bytes, error_message, probe_size, &final_url, &content_type)) {
            return false;
        }
        if (!IsDocumentContentType(content_type)) {
            file_url = final_url;
            return true;
        }
        std::string resolved;
        if (!ResolveDataLinkFileUrl(final_url, resolved, error_message, fetch_text) || !ValidateRemoteFetchUrl(resolved, error_message)) {
            return false;
        }
        url = resolved;
    }

    error_message = "could not resolve a FITS file from the SIA result";
    return false;
}

} // namespace carta
