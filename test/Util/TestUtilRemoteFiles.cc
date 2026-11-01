/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <curl/curl.h>
#include <gtest/gtest.h>
#include <map>
#include "Util/RemoteFiles.h"

namespace {

CARTA::RemoteFileRequest HipsRequest() {
    CARTA::RemoteFileRequest request;
    request.set_hips("CDS/P/DSS2/color");
    request.set_width(1024);
    request.set_height(768);
    request.set_coordsys("icrs");
    request.set_projection("TAN");
    request.set_fov(1.5);
    request.set_object("M31");
    return request;
}

SizeProbeFetcher FakeProbe(std::map<std::string, SizeProbeResult> responses, std::vector<std::string>* requested = nullptr) {
    return [responses = std::move(responses), requested](const std::string& url, SizeProbeResult& result) {
        if (requested) {
            requested->push_back(url);
        }
        auto it = responses.find(url);
        if (it == responses.end()) {
            result.error = "unexpected url " + url;
            return false;
        }
        result = it->second;
        return true;
    };
}

} // namespace

TEST(RemoteFileHips, GeneratesHipsUrl) {
    std::string url, error_message;
    EXPECT_TRUE(GenerateUrlFromRequest(HipsRequest(), url, error_message));
    EXPECT_EQ(url.find("https://alasky.cds.unistra.fr/hips-image-services/hips2fits?"), 0);
}

TEST(RemoteFileHips, MissingFieldsRejected) {
    CARTA::RemoteFileRequest request;
    request.set_hips("CDS/P/DSS2/color");
    std::string url, error_message;
    EXPECT_FALSE(GenerateUrlFromRequest(request, url, error_message));
    EXPECT_NE(error_message.find("width and height"), std::string::npos);
}

TEST(RemoteFetchUrl, AcceptsHttpAndHttpsUrls) {
    std::string error_message;
    EXPECT_TRUE(ValidateRemoteFetchUrl("https://example.org/data/image.fits", error_message));
    EXPECT_TRUE(ValidateRemoteFetchUrl("http://example.org/data/image.fits?a=1&b=2", error_message));
}

TEST(RemoteFetchUrl, RejectsNonHttpSchemes) {
    std::string error_message;
    EXPECT_FALSE(ValidateRemoteFetchUrl("ftp://example.org/x.fits", error_message));
    EXPECT_FALSE(ValidateRemoteFetchUrl("file:///etc/passwd", error_message));
    EXPECT_FALSE(ValidateRemoteFetchUrl("mem://x", error_message));
    EXPECT_FALSE(ValidateRemoteFetchUrl("example.org/x.fits", error_message));
}

TEST(RemoteFetchUrl, RejectsCfitsioMetacharacters) {
    // cfitsio extended filename syntax: output file, filters, and other specifiers
    std::string error_message;
    EXPECT_FALSE(ValidateRemoteFetchUrl("https://example.org/x.fits(out.fits)", error_message));
    EXPECT_FALSE(ValidateRemoteFetchUrl("https://example.org/x.fits[0]", error_message));
    EXPECT_FALSE(ValidateRemoteFetchUrl("https://example.org/x.fits y.fits", error_message));
    EXPECT_FALSE(ValidateRemoteFetchUrl("https://example.org/x.fits\ty", error_message));
    EXPECT_FALSE(ValidateRemoteFetchUrl("https://user@example.org/x.fits", error_message));
    EXPECT_FALSE(ValidateRemoteFetchUrl("https://example.org/x'y.fits", error_message));
    EXPECT_FALSE(ValidateRemoteFetchUrl("https://example.org/x\"y.fits", error_message));
    EXPECT_FALSE(ValidateRemoteFetchUrl("https://example.org/x|y.fits", error_message));
    EXPECT_FALSE(ValidateRemoteFetchUrl("https://example.org/x\\y.fits", error_message));
    EXPECT_FALSE(ValidateRemoteFetchUrl("https://example.org/x\xc3\xa9.fits", error_message));
}

TEST(RemoteFileDisplayName, DefaultNameFromIndex) {
    EXPECT_EQ(RemoteFileDisplayName("", 0), "remote_file.fits");
    EXPECT_EQ(RemoteFileDisplayName("", 3), "remote_file3.fits");
}

TEST(RemoteFileDisplayName, UsesSanitizedRequestedName) {
    EXPECT_EQ(RemoteFileDisplayName("m31_dss2.fits", 0), "m31_dss2.fits");
    EXPECT_EQ(RemoteFileDisplayName("NEOS SCI m31", 1), "NEOS SCI m31");
}

TEST(RemoteFileDisplayName, StripsPathComponents) {
    EXPECT_EQ(RemoteFileDisplayName("../../etc/passwd", 0), "passwd");
    EXPECT_EQ(RemoteFileDisplayName("foo/bar.fits", 0), "bar.fits");
}

TEST(RemoteFileDisplayName, FallsBackWhenNothingUsable) {
    EXPECT_EQ(RemoteFileDisplayName("../..///", 2), "remote_file2.fits");
    EXPECT_EQ(RemoteFileDisplayName("\x01\x02", 0), "remote_file.fits");
}

TEST(RemoteFileProbeResult, AcceptsCompletedTransfer) {
    EXPECT_TRUE(IsProbeResultUsable(CURLE_OK, false));
    EXPECT_TRUE(IsProbeResultUsable(CURLE_OK, true));
}

TEST(RemoteFileProbeResult, AcceptsTransfersAbortedAtTheBody) {
    // The probe stops the transfer itself; servers which ignore the range request stream the whole file
    EXPECT_TRUE(IsProbeResultUsable(CURLE_WRITE_ERROR, true));
    EXPECT_TRUE(IsProbeResultUsable(CURLE_RECV_ERROR, true));
    EXPECT_TRUE(IsProbeResultUsable(CURLE_PARTIAL_FILE, true));
}

TEST(RemoteFileProbeResult, RejectsFailuresBeforeTheHeadersAreComplete) {
    EXPECT_FALSE(IsProbeResultUsable(CURLE_COULDNT_CONNECT, false));
    EXPECT_FALSE(IsProbeResultUsable(CURLE_OPERATION_TIMEDOUT, false));
    EXPECT_FALSE(IsProbeResultUsable(CURLE_RECV_ERROR, false));
}

TEST(RemoteFileSizeCheck, AcceptsFileWithinLimit) {
    std::string error_message;
    auto fetcher = FakeProbe({{"https://example.org/x.fits", {200, "", 1000}}});
    EXPECT_TRUE(CheckRemoteFileSize("https://example.org/x.fits", 2000, error_message, fetcher));
}

TEST(RemoteFileSizeCheck, RejectsFileOverLimit) {
    std::string error_message;
    auto fetcher = FakeProbe({{"https://example.org/x.fits", {200, "", 5000}}});
    EXPECT_FALSE(CheckRemoteFileSize("https://example.org/x.fits", 2000, error_message, fetcher));
    EXPECT_NE(error_message.find("size"), std::string::npos);
}

TEST(RemoteFileSizeCheck, AcceptsUnknownSizeWhichTheDownloadItselfBounds) {
    // Chunked responses report no length; CurlDownloadFile still aborts at the limit
    std::string error_message;
    auto fetcher = FakeProbe({{"https://example.org/x.fits", {200, "", -1}}});
    EXPECT_TRUE(CheckRemoteFileSize("https://example.org/x.fits", 2000, error_message, fetcher));
}

TEST(RemoteFileSizeCheck, DisabledLimitStillResolvesTheFinalUrl) {
    auto probe = FakeProbe({{"https://example.org/a.fits", {200, "", -1, "application/fits", ""}}});
    std::string error_message, final_url, content_type;
    EXPECT_TRUE(CheckRemoteFileSize("https://example.org/a.fits", 0, error_message, probe, &final_url, &content_type));
    EXPECT_EQ(final_url, "https://example.org/a.fits");
    EXPECT_EQ(content_type, "application/fits");
}

TEST(RemoteFileSizeCheck, DisabledLimitAcceptsUnknownAndOversizedFiles) {
    auto probe = FakeProbe({{"https://example.org/huge.fits", {200, "", 999999999999, "application/fits", ""}}});
    std::string error_message, final_url;
    EXPECT_TRUE(CheckRemoteFileSize("https://example.org/huge.fits", 0, error_message, probe, &final_url, nullptr));
    EXPECT_EQ(final_url, "https://example.org/huge.fits");
}

TEST(RemoteFileSizeCheck, DisabledLimitStillFollowsRedirects) {
    // The probe resolves the target and its content type, which the caller needs whether or not a limit applies
    std::string error_message, final_url;
    auto fetcher = FakeProbe({{"https://example.org/x.fits", {302, "https://data.example.org/y.fits", -1}},
        {"https://data.example.org/y.fits", {200, "", -1, "application/fits", ""}}});
    EXPECT_TRUE(CheckRemoteFileSize("https://example.org/x.fits", 0, error_message, fetcher, &final_url, nullptr));
    EXPECT_EQ(final_url, "https://data.example.org/y.fits");
}

TEST(RemoteFileSizeCheck, FollowsRedirects) {
    std::string error_message;
    std::vector<std::string> requested;
    auto fetcher = FakeProbe(
        {
            {"https://example.org/x.fits", {302, "https://data.example.org/y.fits", -1}},
            {"https://data.example.org/y.fits", {200, "", 1000}},
        },
        &requested);
    EXPECT_TRUE(CheckRemoteFileSize("https://example.org/x.fits", 2000, error_message, fetcher));
    EXPECT_EQ(requested.size(), 2);
}

TEST(RemoteFileSizeCheck, ReturnsFinalRedirectTarget) {
    // e.g. CADC's locator issues a 303 to a signed storage URL which cfitsio must fetch directly
    std::string error_message, final_url;
    auto fetcher = FakeProbe({
        {"https://example.org/x.fits", {303, "https://data.example.org/signed/x.fits", -1}},
        {"https://data.example.org/signed/x.fits", {200, "", 1000}},
    });
    EXPECT_TRUE(CheckRemoteFileSize("https://example.org/x.fits", 2000, error_message, fetcher, &final_url));
    EXPECT_EQ(final_url, "https://data.example.org/signed/x.fits");
}

TEST(RemoteFileSizeCheck, RejectsRedirectToInvalidUrl) {
    std::string error_message;
    auto fetcher = FakeProbe({
        {"https://example.org/x.fits", {302, "ftp://evil.example.com/y.fits", -1}},
    });
    EXPECT_FALSE(CheckRemoteFileSize("https://example.org/x.fits", 2000, error_message, fetcher));
}

TEST(RemoteFileSizeCheck, RejectsRedirectLoop) {
    std::string error_message;
    auto fetcher = FakeProbe({
        {"https://example.org/x.fits", {302, "https://example.org/x.fits", -1}},
    });
    EXPECT_FALSE(CheckRemoteFileSize("https://example.org/x.fits", 2000, error_message, fetcher));
}

TEST(RemoteFileSizeCheck, RejectsHttpErrorStatus) {
    std::string error_message;
    auto fetcher = FakeProbe({{"https://example.org/x.fits", {404, "", -1}}});
    EXPECT_FALSE(CheckRemoteFileSize("https://example.org/x.fits", 2000, error_message, fetcher));
    EXPECT_NE(error_message.find("404"), std::string::npos);
}

TEST(RemoteFileRedirectUrl, KeepsAbsoluteLocation) {
    EXPECT_EQ(ResolveRelativeUrl("https://example.org/a/b.fits", "https://other.org/c.fits"), "https://other.org/c.fits");
}

TEST(RemoteFileRedirectUrl, ResolvesRootRelativeLocation) {
    EXPECT_EQ(ResolveRelativeUrl("https://example.org/a/b.fits?x=1", "/c/d.fits"), "https://example.org/c/d.fits");
}

TEST(RemoteFileRedirectUrl, ResolvesPathRelativeLocation) {
    EXPECT_EQ(ResolveRelativeUrl("https://example.org/a/b.fits", "d.fits"), "https://example.org/a/d.fits");
}

TEST(RemoteFileRedirectUrl, ResolvesProtocolRelativeLocation) {
    EXPECT_EQ(ResolveRelativeUrl("https://example.org/a/b.fits", "//other.org/c.fits"), "https://other.org/c.fits");
}

TEST(RemoteFileDownloadProgress, ReportsFractionWhenTotalKnown) {
    DownloadProgressThrottle throttle(std::chrono::milliseconds(0));
    float progress(0);
    EXPECT_TRUE(throttle.ShouldReport(250, 1000, progress));
    EXPECT_FLOAT_EQ(progress, 0.25);
}

TEST(RemoteFileDownloadProgress, ReportsNegativeFractionWhenTotalUnknown) {
    DownloadProgressThrottle throttle(std::chrono::milliseconds(0));
    float progress(0);
    EXPECT_TRUE(throttle.ShouldReport(250, -1, progress));
    EXPECT_LT(progress, 0);
}

TEST(RemoteFileDownloadProgress, SuppressesUpdatesWithinTheInterval) {
    DownloadProgressThrottle throttle(std::chrono::milliseconds(60000));
    float progress(0);
    EXPECT_TRUE(throttle.ShouldReport(100, 1000, progress));
    EXPECT_FALSE(throttle.ShouldReport(200, 1000, progress));
    EXPECT_FALSE(throttle.ShouldReport(300, 1000, progress));
}

TEST(RemoteFileDownloadProgress, AlwaysReportsCompletion) {
    DownloadProgressThrottle throttle(std::chrono::milliseconds(60000));
    float progress(0);
    EXPECT_TRUE(throttle.ShouldReport(100, 1000, progress));
    EXPECT_TRUE(throttle.ShouldReport(1000, 1000, progress));
    EXPECT_FLOAT_EQ(progress, 1.0);
}

TEST(RemoteFileDownloadProgress, DoesNotRepeatCompletion) {
    DownloadProgressThrottle throttle(std::chrono::milliseconds(60000));
    float progress(0);
    EXPECT_TRUE(throttle.ShouldReport(1000, 1000, progress));
    EXPECT_FALSE(throttle.ShouldReport(1000, 1000, progress));
}

TEST(RemoteFileDownloadProgress, ResumesWhenANewTransferStarts) {
    // curl reports a redirect body before the real transfer, which must not latch completion
    DownloadProgressThrottle throttle(std::chrono::milliseconds(0));
    float progress(0);
    EXPECT_TRUE(throttle.ShouldReport(169, 169, progress));
    EXPECT_TRUE(throttle.ShouldReport(1000, 100000, progress));
    EXPECT_FLOAT_EQ(progress, 0.01);
    EXPECT_TRUE(throttle.ShouldReport(100000, 100000, progress));
    EXPECT_FLOAT_EQ(progress, 1.0);
}

TEST(RemoteFileDownloadProgress, IgnoresZeroLengthDownloads) {
    DownloadProgressThrottle throttle(std::chrono::milliseconds(0));
    float progress(0);
    EXPECT_FALSE(throttle.ShouldReport(0, 0, progress));
}
