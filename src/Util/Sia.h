/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_UTIL_SIA_H_
#define CARTA_SRC_UTIL_SIA_H_

#include <carta-protobuf/sia_query.pb.h>
#include <functional>
#include <string>
#include <vector>
#include "RemoteFiles.h"

namespace carta {

/** @brief A curated SIA 2.0 service. */
struct SiaService {
    std::string name;
    std::string query_url;
};

/**
 * @brief Parses configured SIA service entries of the form "Name|https://query-url" (or a bare
 * URL, in which case the host is used as the name). Empty or invalid entries are skipped.
 */
std::vector<SiaService> ParseSiaServices(const std::vector<std::string>& entries);

/**
 * @brief Builds the SIA 2.0 query URL from a request: a POS=CIRCLE parameter from the
 * coordinates, plus validated and escaped filter parameters.
 *
 * @param request The query request; `service_url` is used as the base URL and must have been
 * matched against the configured services by the caller.
 * @param url The query URL to be returned.
 * @param error_message A message indicating the reason for failure.
 *
 * @return `true` if the URL was built, `false` for invalid coordinates or filters.
 */
bool BuildSiaQueryUrl(const CARTA::SiaQueryRequest& request, std::string& url, std::string& error_message);

/** @brief The outcome of an HTTP text fetch. */
struct HttpTextResult {
    long status = 0;
    std::string body;
    std::string error;
};

/** @brief Fetches the body of a URL (following redirects). */
using TextFetcher = std::function<bool(const std::string& url, HttpTextResult& result)>;

/** @brief Fetches a text response with libcurl, with size and time limits. */
bool CurlFetchText(const std::string& url, HttpTextResult& result);

/** @brief The loadable location of a query result, cached per session. */
struct SiaResultEntry {
    std::string access_url;
    std::string access_format;
};

/** @brief Whether an access format denotes a DataLink document rather than a direct file. */
bool IsDataLinkFormat(const std::string& access_format);

/**
 * @brief Runs an SIA 2.0 query: fetches the VOTable (retrying with RESPONSEFORMAT=votable/td
 * if the service uses a binary serialization), and maps the ObsCore columns.
 *
 * @param query_url The full query URL from `BuildSiaQueryUrl`.
 * @param records The loadable dataset records, with 1-based indices.
 * @param error_message A message indicating the reason for failure (including QUERY_STATUS
 * errors reported by the service).
 * @param fetch_text The fetch implementation (injectable for testing).
 *
 * @return `true` if the query succeeded (possibly with zero records), `false` otherwise.
 */
bool RunSiaQuery(const std::string& query_url, std::vector<CARTA::SiaRecord>& records, std::string& error_message,
    const TextFetcher& fetch_text = CurlFetchText);

/**
 * @brief Resolves a DataLink document to the first loadable file: the first row with
 * semantics "#this", an access URL, no error message, and a FITS (or unspecified) content type.
 *
 * @param links_url The DataLink access URL from an SIA record.
 * @param file_url The resolved file URL to be returned.
 * @param error_message A message indicating the reason for failure.
 * @param fetch_text The fetch implementation (injectable for testing).
 *
 * @return `true` if a loadable file was found, `false` otherwise.
 */
bool ResolveDataLinkFileUrl(
    const std::string& links_url, std::string& file_url, std::string& error_message, const TextFetcher& fetch_text = CurlFetchText);

/**
 * @brief Resolves a query result to a loadable FITS URL: resolves DataLink documents (both
 * those declared by the record's access format and those an access URL redirects to), and
 * checks the file size.
 *
 * @param access_url The record's access URL.
 * @param is_datalink Whether the record's access format declares a DataLink document.
 * @param max_size_bytes The maximum allowed file size; a value <= 0 disables the check.
 * @param file_url The resolved file URL to be returned.
 * @param error_message A message indicating the reason for failure.
 * @param fetch_text The text fetch implementation (injectable for testing).
 * @param probe_size The size probe implementation (injectable for testing).
 *
 * @return `true` if a loadable file URL was resolved, `false` otherwise.
 */
bool ResolveSiaFileUrl(const std::string& access_url, bool is_datalink, int64_t max_size_bytes, std::string& file_url,
    std::string& error_message, const TextFetcher& fetch_text = CurlFetchText,
    const SizeProbeFetcher& probe_size = CurlProbeRemoteFileSize);

} // namespace carta

#endif // CARTA_SRC_UTIL_SIA_H_
