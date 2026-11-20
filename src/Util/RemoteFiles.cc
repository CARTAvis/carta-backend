/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "RemoteFiles.h"
#include <curl/curl.h>
#include <fcntl.h>
#include <spdlog/fmt/fmt.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <mutex>
#include <regex>
#include "FileSystem.h"
#include "String.h"

namespace {

// Reject anything cfitsio could interpret as extended filename syntax (output file, HDU/filter specifiers),
// plus userinfo, quotes and other characters with no place in an archive data URL.
bool HasForbiddenUrlCharacter(const std::string& url) {
    const std::string forbidden("()[]{}<>'\"\\|@`");
    return std::any_of(
        url.begin(), url.end(), [&](unsigned char c) { return c <= 0x20 || c >= 0x7f || forbidden.find(c) != std::string::npos; });
}

} // namespace

bool ValidateRemoteFetchUrl(const std::string& url, std::string& error_message) {
    static const std::regex scheme_regex("^https?://", std::regex::icase);
    if (!std::regex_search(url, scheme_regex)) {
        error_message = "url must start with http:// or https://";
        return false;
    }
    if (HasForbiddenUrlCharacter(url)) {
        error_message = "url contains a forbidden character";
        return false;
    }
    return true;
}

/**
 * @details This function validates the `CARTA::RemoteFileRequest` parameters and constructs a URL
 * to fetch an image from the HiPS service. If any required parameters are missing or invalid,
 * it returns `false` and provides an appropriate error message.
 *
 * @note The function ensures that all required parameters are present and valid before constructing the URL.
 *       If `wcs` is not provided, additional fields such as `coordsys`, `projection`, `fov`, and `object` or `(ra, dec)` must be specified.
 */
bool GenerateUrlFromRequest(const CARTA::RemoteFileRequest& request, std::string& url, std::string& error_message) {
    // Check for required fields
    if (request.hips().empty()) {
        error_message = "hips ID or keyword is required";
        return false;
    }
    if (!std::isfinite(request.width()) || !std::isfinite(request.height()) || request.width() <= 0 || request.height() <= 0) {
        error_message = "width and height are required";
        return false;
    }
    if (request.width() * request.height() > HIPS_MAX_PIXELS) {
        error_message = "requested image size exceeds maximum pixel count";
        return false;
    }
    if (!request.coordsys().empty() && request.coordsys() != "icrs" && request.coordsys() != "galactic") {
        error_message = "invalid coordinate system";
        return false;
    }
    if (request.wcs().empty()) {
        if (request.coordsys().empty()) {
            error_message = "coordsys is required if wcs is not provided";
            return false;
        } else if (request.projection().empty()) {
            error_message = "projection is required if wcs is not provided";
            return false;
        } else if (!std::isfinite(request.fov()) || request.fov() <= 0) {
            error_message = "fov is required if wcs is not provided";
            return false;
        } else if (request.object().empty() && (!std::isfinite(request.ra()) || !std::isfinite(request.dec()))) {
            error_message = "object or (ra, dec) are required if wcs is not provided";
            return false;
        }
    }

    url = fmt::format(
        "{}?hips={}&format=fits&width={}&height={}", HIPS_BASE_URL, SafeStringEscape(request.hips()), request.width(), request.height());

    if (!request.wcs().empty()) {
        url += fmt::format("&wcs={}", SafeStringEscape(request.wcs()));
    }
    if (!request.projection().empty()) {
        url += fmt::format("&projection={}", SafeStringEscape(request.projection()));
    }
    if (std::isfinite(request.fov()) && request.fov() > 0) {
        url += fmt::format("&fov={}", request.fov());
    }
    if (std::isfinite(request.ra()) && std::isfinite(request.dec())) {
        url += fmt::format("&ra={}&dec={}", request.ra(), request.dec());
    }
    if (!request.coordsys().empty()) {
        url += fmt::format("&coordsys={}", SafeStringEscape(request.coordsys()));
    }
    if (std::isfinite(request.rotation_angle())) {
        url += fmt::format("&rotation_angle={}", request.rotation_angle());
    }
    if (!request.object().empty()) {
        url += fmt::format("&object={}", SafeStringEscape(request.object()));
    }

    return true;
}

namespace {

struct SizeProbeContext {
    int64_t range_total = -1;
    std::string content_type;
    std::string location;
    bool body_reached = false;
};

std::string HeaderValue(const std::string& header) {
    auto value = header.substr(header.find(':') + 1);
    auto start = value.find_first_not_of(" \t");
    auto end = value.find_last_not_of(" \r\n");
    return start == std::string::npos ? "" : value.substr(start, end - start + 1);
}

size_t SizeProbeHeaderCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
    auto* context = static_cast<SizeProbeContext*>(userdata);
    std::string header(buffer, size * nitems);
    std::string lower(header);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    // A 206 reply reports the full size as "Content-Range: bytes 0-0/total"
    if (lower.rfind("content-type:", 0) == 0) {
        context->content_type = HeaderValue(header);
    }
    if (lower.rfind("location:", 0) == 0) {
        context->location = HeaderValue(header);
    }
    if (lower.rfind("content-range:", 0) == 0) {
        auto slash = header.rfind('/');
        if (slash != std::string::npos) {
            try {
                context->range_total = std::stoll(header.substr(slash + 1));
            } catch (const std::exception&) {
                context->range_total = -1;
            }
        }
    }
    return size * nitems;
}

size_t SizeProbeBodyCallback(char*, size_t, size_t, void* userdata) {
    // Headers have arrived; abort before downloading the body
    static_cast<SizeProbeContext*>(userdata)->body_reached = true;
    return 0;
}

} // namespace

std::string ResolveRelativeUrl(const std::string& base_url, const std::string& location) {
    if (location.rfind("http://", 0) == 0 || location.rfind("https://", 0) == 0) {
        return location;
    }

    auto scheme_end = base_url.find("://");
    if (scheme_end == std::string::npos) {
        return location;
    }
    if (location.rfind("//", 0) == 0) {
        return base_url.substr(0, scheme_end + 1) + location;
    }

    auto host_end = base_url.find('/', scheme_end + 3);
    std::string origin = host_end == std::string::npos ? base_url : base_url.substr(0, host_end);
    if (location.rfind('/', 0) == 0) {
        return origin + location;
    }

    auto path = host_end == std::string::npos ? std::string("/") : base_url.substr(host_end);
    path = path.substr(0, path.find_first_of("?#"));
    auto last_slash = path.rfind('/');
    return origin + path.substr(0, last_slash + 1) + location;
}

bool IsProbeResultUsable(int curl_code, bool body_reached) {
    return curl_code == CURLE_OK || body_reached;
}

bool IsDocumentContentType(const std::string& content_type) {
    std::string lower_type(content_type);
    std::transform(lower_type.begin(), lower_type.end(), lower_type.begin(), [](unsigned char c) { return std::tolower(c); });
    return lower_type.find("votable") != std::string::npos || lower_type.find("datalink") != std::string::npos;
}

bool CurlProbeRemoteFileSize(const std::string& url, SizeProbeResult& result) {
    // libcurl is also used by cfitsio; global init is reference-counted and safe to repeat
    static std::once_flag curl_init_flag;
    std::call_once(curl_init_flag, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });

    CURL* curl = curl_easy_init();
    if (!curl) {
        result.error = "failed to initialise curl";
        return false;
    }

    // A 0-byte ranged GET rather than HEAD: some archive servers (e.g. DaCHS product delivery) hang on HEAD
    SizeProbeContext context;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_RANGE, "0-0");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "carta-backend");
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, SizeProbeHeaderCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &context);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, SizeProbeBodyCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &context);

    auto curl_result = curl_easy_perform(curl);
    if (!IsProbeResultUsable(curl_result, context.body_reached)) {
        result.error = curl_easy_strerror(curl_result);
        curl_easy_cleanup(curl);
        return false;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status);
    if (!context.location.empty()) {
        result.redirect_url = ResolveRelativeUrl(url, context.location);
    } else {
        char* redirect_url(nullptr);
        curl_easy_getinfo(curl, CURLINFO_REDIRECT_URL, &redirect_url);
        result.redirect_url = redirect_url ? redirect_url : "";
    }
    result.content_type = context.content_type;

    if (context.range_total >= 0) {
        result.content_length = context.range_total;
    } else if (result.status != 206) {
        // Server ignored the range request; Content-Length is the full size
        curl_off_t content_length(-1);
        curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &content_length);
        result.content_length = content_length;
    }
    curl_easy_cleanup(curl);
    return true;
}

bool CheckRemoteFileSize(const std::string& url, int64_t max_size_bytes, std::string& error_message, const SizeProbeFetcher& probe_size,
    std::string* final_url, std::string* final_content_type) {
    // Callers use the resolved URL whether or not a limit is enforced, so the probe still runs
    const bool enforce_limit = max_size_bytes > 0;

    const int max_redirects(5);
    std::string current_url(url);

    for (int hop = 0; hop <= max_redirects; ++hop) {
        if (!ValidateRemoteFetchUrl(current_url, error_message)) {
            return false;
        }

        SizeProbeResult result;
        if (!probe_size(current_url, result)) {
            error_message = fmt::format("checking remote file size failed: {}", result.error);
            return false;
        }

        if (result.status >= 300 && result.status < 400) {
            if (result.redirect_url.empty() || result.redirect_url == current_url) {
                error_message = "remote server returned an invalid redirect";
                return false;
            }
            current_url = result.redirect_url;
            continue;
        }
        if (result.status < 200 || result.status >= 300) {
            error_message = fmt::format("remote file size check returned HTTP status {}", result.status);
            return false;
        }
        if (final_content_type) {
            *final_content_type = result.content_type;
        }
        bool is_document = IsDocumentContentType(result.content_type);

        if (enforce_limit && !is_document && result.content_length > max_size_bytes) {
            error_message = fmt::format(
                "remote file size {:.1f} MB exceeds the maximum of {} MB", result.content_length / 1048576.0, max_size_bytes / 1048576);
            return false;
        }
        if (final_url) {
            // The download starts from the resolved target rather than walking the chain again
            *final_url = current_url;
        }
        return true;
    }

    error_message = "remote file size check failed: too many redirects";
    return false;
}

namespace {

struct DownloadContext {
    std::ofstream file;
    int64_t written = 0;
    int64_t max_bytes = 0;
    bool over_limit = false;
};

size_t DownloadWriteCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
    auto* context = static_cast<DownloadContext*>(userdata);
    auto bytes = static_cast<int64_t>(size * nitems);
    if (context->max_bytes > 0 && context->written + bytes > context->max_bytes) {
        context->over_limit = true;
        return 0;
    }
    context->file.write(buffer, bytes);
    context->written += bytes;
    return context->file.good() ? size * nitems : 0;
}

} // namespace

bool DownloadProgressThrottle::ShouldReport(int64_t downloaded, int64_t total, float& progress) {
    if (downloaded <= 0) {
        return false;
    }

    // curl reports redirect bodies before the file itself; each is a separate transfer
    if (total != _last_total || downloaded < _last_downloaded) {
        _started = false;
        _completed = false;
    }
    _last_downloaded = downloaded;
    _last_total = total;

    if (_completed) {
        return false;
    }

    bool complete = total > 0 && downloaded >= total;
    auto now = std::chrono::steady_clock::now();
    if (!complete && _started && (now - _last_report) < _interval) {
        return false;
    }

    progress = total > 0 ? static_cast<float>(static_cast<double>(downloaded) / static_cast<double>(total)) : -1.0F;
    _last_report = now;
    _started = true;
    _completed = complete;
    return true;
}

namespace {

int DownloadProgressCurlCallback(void* userdata, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t) {
    const auto& report = *static_cast<const DownloadProgressCallback*>(userdata);
    if (report) {
        report(dlnow, dltotal > 0 ? dltotal : -1);
    }
    return 0;
}

} // namespace

bool CurlDownloadFile(const std::string& url, const std::string& path, int64_t max_size_bytes, std::string& error_message,
    const DownloadProgressCallback& report_progress) {
    // libcurl is also used by cfitsio; global init is reference-counted and safe to repeat
    static std::once_flag curl_init_flag;
    std::call_once(curl_init_flag, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });

    CURL* curl = curl_easy_init();
    if (!curl) {
        error_message = "failed to initialise curl";
        return false;
    }

    DownloadContext context;
    context.max_bytes = max_size_bytes;
    // Created exclusively, so a symlink planted at a guessable path in the temp directory is not written through
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, S_IRUSR | S_IWUSR);
    if (fd >= 0) {
        close(fd);
        context.file.open(path, std::ios::binary);
    }
    if (!context.file.good()) {
        error_message = fmt::format("could not create temporary file {}", path);
        curl_easy_cleanup(curl);
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "carta-backend");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, DownloadWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &context);
    if (report_progress) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, DownloadProgressCurlCallback);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &report_progress);
    }

    auto curl_result = curl_easy_perform(curl);
    context.file.close();
    curl_easy_cleanup(curl);

    if (curl_result != CURLE_OK) {
        if (context.over_limit) {
            error_message = fmt::format("remote file exceeds the maximum size of {} MB", max_size_bytes / 1048576);
        } else {
            error_message = fmt::format("downloading remote file failed: {}", curl_easy_strerror(curl_result));
        }
        std::error_code fs_error;
        fs::remove(path, fs_error);
        return false;
    }
    return true;
}

std::string RemoteFileDisplayName(const std::string& requested_filename, int index) {
    // Basename only, printable characters only
    std::string name = fs::path(requested_filename).filename().string();
    name.erase(std::remove_if(name.begin(), name.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; }), name.end());

    if (name.empty() || name == "." || name == "..") {
        return index > 0 ? fmt::format("remote_file{}.fits", index) : "remote_file.fits";
    }
    return name;
}
