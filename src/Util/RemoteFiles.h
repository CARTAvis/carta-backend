/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_UTIL_REMOTEFILES_H_
#define CARTA_SRC_UTIL_REMOTEFILES_H_

#include <carta-protobuf/remote_file_request.pb.h>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// shouldn't these be in an ENV file?
const auto HIPS_BASE_URL = "https://alasky.cds.unistra.fr/hips-image-services/hips2fits";
const auto HIPS_MAX_PIXELS = 50e6;

/**
 * @brief Generates a hips2fits URL for a remote file request based on provided parameters.
 *
 * @param request The `CARTA::RemoteFileRequest` containing the request parameters.
 * @param url The generated URL to be returned if the request is valid.
 * @param error_message A message indicating the reason for failure if the request is invalid.
 *
 * @return `true` if the URL was successfully generated, `false` otherwise.
 */
bool GenerateUrlFromRequest(const CARTA::RemoteFileRequest& request, std::string& url, std::string& error_message);

/**
 * @brief Validates a URL before the backend fetches it: http(s) scheme only, and no characters
 * cfitsio could interpret as extended filename syntax.
 *
 * @param url The URL to validate.
 * @param error_message A message indicating the reason for failure.
 *
 * @return `true` if the URL is safe to fetch, `false` otherwise.
 */
bool ValidateRemoteFetchUrl(const std::string& url, std::string& error_message);

/** @brief The outcome of a remote file size probe. */
struct SizeProbeResult {
    long status = 0;
    std::string redirect_url;
    int64_t content_length = -1;
    std::string content_type;
    std::string error;
};

/** @brief Resolves an HTTP Location header value against the URL it was returned from. */
std::string ResolveRelativeUrl(const std::string& base_url, const std::string& location);

/** @brief Probes a URL's HTTP status and file size without downloading the body or following redirects. */
using SizeProbeFetcher = std::function<bool(const std::string& url, SizeProbeResult& result)>;

/** @brief Probes a URL with a 0-byte ranged GET using libcurl (HEAD hangs on some archive servers). */
bool CurlProbeRemoteFileSize(const std::string& url, SizeProbeResult& result);

/** @brief Whether a probe transfer (CURLcode) is usable: the probe aborts once the body is reached, headers in hand. */
bool IsProbeResultUsable(int curl_code, bool body_reached);

/** @brief Whether a content type is a service document (a VOTable or DataLink response) rather than file data. */
bool IsDocumentContentType(const std::string& content_type);

/**
 * @brief Checks the size of a remote file before it is fetched.
 *
 * Redirects are followed manually and every hop is re-validated with `ValidateRemoteFetchUrl`.
 *
 * @param url The remote file URL (already validated).
 * @param max_size_bytes The maximum allowed file size; a value <= 0 disables the check.
 * @param error_message A message indicating the reason for failure.
 * @param probe_size The probe implementation (injectable for testing).
 * @param final_url If given, receives the final URL after redirects (which should be fetched directly).
 * @param final_content_type If given, receives the content type of the final response.
 *
 * @return `true` if the file is within the size limit, `false` otherwise.
 */
bool CheckRemoteFileSize(const std::string& url, int64_t max_size_bytes, std::string& error_message,
    const SizeProbeFetcher& probe_size = CurlProbeRemoteFileSize, std::string* final_url = nullptr,
    std::string* final_content_type = nullptr);

/**
 * @brief Rate-limits download progress reports. Completion is always reported once, so a
 * client never sees a stalled bar, and progress is negative when the total size is unknown.
 */
class DownloadProgressThrottle {
public:
    DownloadProgressThrottle(std::chrono::milliseconds interval = std::chrono::milliseconds(200)) : _interval(interval) {}
    bool ShouldReport(int64_t downloaded, int64_t total, float& progress);

private:
    std::chrono::milliseconds _interval;
    std::chrono::steady_clock::time_point _last_report;
    int64_t _last_downloaded = 0;
    int64_t _last_total = 0;
    bool _started = false;
    bool _completed = false;
};

/** @brief Reports download progress: bytes downloaded so far and the total (-1 if unknown). */
using DownloadProgressCallback = std::function<void(int64_t downloaded, int64_t total)>;

/**
 * @brief Downloads a remote file with libcurl, aborting beyond the byte limit. No
 * Accept-Encoding is negotiated (some archive stores use encodings libcurl rejects, e.g.
 * CADC's `x-fits`), which is also why cfitsio cannot fetch these URLs itself.
 *
 * @param url The remote file URL (already validated, redirects already resolved).
 * @param path The local path to write to.
 * @param max_size_bytes The maximum download size; a value <= 0 means unlimited.
 * @param error_message A message indicating the reason for failure.
 *
 * @return `true` if the download completed, `false` otherwise.
 */
bool CurlDownloadFile(const std::string& url, const std::string& path, int64_t max_size_bytes, std::string& error_message,
    const DownloadProgressCallback& report_progress = nullptr);

/**
 * @brief Returns the display filename for an opened remote file.
 *
 * @param requested_filename The client-requested display name; sanitized to its basename.
 * @param index Per-session remote file counter, used for the fallback name.
 *
 * @return The sanitized requested name, or `remote_file[index].fits` if unusable.
 */
std::string RemoteFileDisplayName(const std::string& requested_filename, int index);

#endif // CARTA_SRC_UTIL_REMOTEFILES_H_
