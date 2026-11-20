/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <gtest/gtest.h>
#include <cmath>
#include <map>
#include "Util/RemoteFiles.h"
#include "Util/Sia.h"

using namespace carta;

namespace {

const std::string kObscoreVotable = R"(<?xml version="1.0" encoding="UTF-8"?>
<VOTABLE xmlns="http://www.ivoa.net/xml/VOTable/v1.3" version="1.3">
  <RESOURCE type="results">
    <INFO name="QUERY_STATUS" value="OK"/>
    <TABLE>
      <FIELD name="obs_id" datatype="char" arraysize="*"/>
      <FIELD name="target_name" datatype="char" arraysize="*"/>
      <FIELD name="obs_collection" datatype="char" arraysize="*"/>
      <FIELD name="instrument_name" datatype="char" arraysize="*"/>
      <FIELD name="dataproduct_type" datatype="char" arraysize="*"/>
      <FIELD name="calib_level" datatype="int"/>
      <FIELD name="s_ra" datatype="double" unit="deg"/>
      <FIELD name="s_dec" datatype="double" unit="deg"/>
      <FIELD name="s_fov" datatype="double" unit="deg"/>
      <FIELD name="access_url" datatype="char" arraysize="*" ucd="meta.ref.url"/>
      <FIELD name="access_format" datatype="char" arraysize="*"/>
      <FIELD name="access_estsize" datatype="long" unit="kbyte"/>
      <DATA>
        <TABLEDATA>
          <TR>
            <TD>obs-1</TD><TD>M31</TD><TD>SURVEY</TD><TD>Cam1</TD><TD>image</TD><TD>2</TD>
            <TD>10.68</TD><TD>41.27</TD><TD>1.0</TD>
            <TD>https://example.org/data/obs-1.fits</TD><TD>application/fits</TD><TD>2048</TD>
          </TR>
          <TR>
            <TD>obs-2</TD><TD>M31</TD><TD>SURVEY</TD><TD>Cam1</TD><TD>cube</TD><TD>3</TD>
            <TD>10.7</TD><TD>41.3</TD><TD></TD>
            <TD>https://example.org/datalink?ID=obs-2</TD><TD>application/x-votable+xml;content=datalink</TD><TD></TD>
          </TR>
        </TABLEDATA>
      </DATA>
    </TABLE>
  </RESOURCE>
</VOTABLE>)";

// Services such as IRSA list every product of an observation, not just the images
const std::string kMixedProductVotable = R"(<?xml version="1.0" encoding="UTF-8"?>
<VOTABLE xmlns="http://www.ivoa.net/xml/VOTable/v1.3" version="1.3">
  <RESOURCE type="results">
    <TABLE>
      <FIELD name="obs_id" datatype="char" arraysize="*"/>
      <FIELD name="access_url" datatype="char" arraysize="*" ucd="meta.ref.url"/>
      <FIELD name="access_format" datatype="char" arraysize="*"/>
      <DATA>
        <TABLEDATA>
          <TR><TD>readme-product</TD><TD>https://example.org/QualityAnalysis.README</TD><TD>plain/text</TD></TR>
          <TR><TD>fits-product</TD><TD>https://example.org/image.fits</TD><TD>image/fits</TD></TR>
          <TR><TD>html-product</TD><TD>https://example.org/page.html</TD><TD>text/html</TD></TR>
          <TR><TD>datalink-product</TD><TD>https://example.org/links?ID=1</TD><TD>application/x-votable+xml;content=datalink</TD></TR>
          <TR><TD>unknown-format-product</TD><TD>https://example.org/mystery</TD><TD></TD></TR>
        </TABLEDATA>
      </DATA>
    </TABLE>
  </RESOURCE>
</VOTABLE>)";

const std::string kUcdFallbackVotable = R"(<?xml version="1.0" encoding="UTF-8"?>
<VOTABLE xmlns="http://www.ivoa.net/xml/VOTable/v1.3" version="1.3">
  <RESOURCE type="results">
    <TABLE>
      <FIELD name="accref" datatype="char" arraysize="*" ucd="meta.ref.url"/>
      <FIELD name="mime" datatype="char" arraysize="*" ucd="meta.code.mime"/>
      <FIELD name="obs_id" datatype="char" arraysize="*"/>
      <DATA>
        <TABLEDATA>
          <TR><TD>https://example.org/x.fits</TD><TD>image/fits</TD><TD>obs-9</TD></TR>
        </TABLEDATA>
      </DATA>
    </TABLE>
  </RESOURCE>
</VOTABLE>)";

// ObsCore has no abstract, but services publish short free-text fields under differing names
const std::string kDescriptiveVotable = R"(<?xml version="1.0" encoding="UTF-8"?>
<VOTABLE xmlns="http://www.ivoa.net/xml/VOTable/v1.3" version="1.3">
  <RESOURCE type="results">
    <TABLE>
      <FIELD name="obs_id" datatype="char" arraysize="*"/>
      <FIELD name="obs_title" datatype="char" arraysize="*"/>
      <FIELD name="dataproduct_subtype" datatype="char" arraysize="*"/>
      <FIELD name="energy_bandpassname" datatype="char" arraysize="*"/>
      <FIELD name="access_url" datatype="char" arraysize="*" ucd="meta.ref.url"/>
      <DATA>
        <TABLEDATA>
          <TR><TD>obs-1</TD><TD>Byurakan fbs1214 (IIF)</TD><TD>science</TD><TD>IIF</TD><TD>https://example.org/a.fits</TD></TR>
        </TABLEDATA>
      </DATA>
    </TABLE>
  </RESOURCE>
</VOTABLE>)";

const std::string kProposalVotable = R"(<?xml version="1.0" encoding="UTF-8"?>
<VOTABLE xmlns="http://www.ivoa.net/xml/VOTable/v1.3" version="1.3">
  <RESOURCE type="results">
    <TABLE>
      <FIELD name="obs_id" datatype="char" arraysize="*"/>
      <FIELD name="proposal_title" datatype="char" arraysize="*"/>
      <FIELD name="bandpass_name" datatype="char" arraysize="*"/>
      <FIELD name="access_url" datatype="char" arraysize="*" ucd="meta.ref.url"/>
      <DATA>
        <TABLEDATA>
          <TR><TD>obs-2</TD><TD>The Hubble Constant</TD><TD>IRAC1</TD><TD>https://example.org/b.fits</TD></TR>
        </TABLEDATA>
      </DATA>
    </TABLE>
  </RESOURCE>
</VOTABLE>)";

// Services declare ObsCore UCDs with qualifiers, and may not use the standard column names
const std::string kQualifiedUcdVotable = R"(<?xml version="1.0" encoding="UTF-8"?>
<VOTABLE xmlns="http://www.ivoa.net/xml/VOTable/v1.3" version="1.3">
  <RESOURCE type="results">
    <TABLE>
      <FIELD name="accref" datatype="char" arraysize="*" ucd="meta.ref.url;meta.dataset"/>
      <FIELD name="ra_deg" datatype="double" ucd="pos.eq.ra;meta.main"/>
      <FIELD name="de_deg" datatype="double" ucd="pos.eq.dec;meta.main"/>
      <DATA>
        <TABLEDATA>
          <TR><TD>https://example.org/q.fits</TD><TD>10.68</TD><TD>41.27</TD></TR>
        </TABLEDATA>
      </DATA>
    </TABLE>
  </RESOURCE>
</VOTABLE>)";

const std::string kBinaryVotable = R"(<?xml version="1.0" encoding="UTF-8"?>
<VOTABLE xmlns="http://www.ivoa.net/xml/VOTable/v1.3" version="1.3">
  <RESOURCE type="results">
    <TABLE>
      <FIELD name="obs_id" datatype="char" arraysize="*"/>
      <DATA>
        <BINARY><STREAM encoding="base64">AAA=</STREAM></BINARY>
      </DATA>
    </TABLE>
  </RESOURCE>
</VOTABLE>)";

const std::string kErrorVotable = R"(<?xml version="1.0" encoding="UTF-8"?>
<VOTABLE xmlns="http://www.ivoa.net/xml/VOTable/v1.3" version="1.3">
  <RESOURCE type="results">
    <INFO name="QUERY_STATUS" value="ERROR">UsageFault: invalid POS value</INFO>
  </RESOURCE>
</VOTABLE>)";

const std::string kEmptyTableVotable = R"(<?xml version="1.0" encoding="UTF-8"?>
<VOTABLE xmlns="http://www.ivoa.net/xml/VOTable/v1.3" version="1.3">
  <RESOURCE type="results">
    <INFO name="QUERY_STATUS" value="OK"/>
    <TABLE>
      <FIELD name="obs_id" datatype="char" arraysize="*"/>
    </TABLE>
  </RESOURCE>
</VOTABLE>)";

const std::string kDataLinkVotable = R"(<?xml version="1.0" encoding="UTF-8"?>
<VOTABLE xmlns="http://www.ivoa.net/xml/VOTable/v1.3" version="1.3">
  <RESOURCE type="results">
    <TABLE>
      <FIELD name="ID" datatype="char" arraysize="*"/>
      <FIELD name="access_url" datatype="char" arraysize="*"/>
      <FIELD name="service_def" datatype="char" arraysize="*"/>
      <FIELD name="error_message" datatype="char" arraysize="*"/>
      <FIELD name="description" datatype="char" arraysize="*"/>
      <FIELD name="semantics" datatype="char" arraysize="*"/>
      <FIELD name="content_type" datatype="char" arraysize="*"/>
      <FIELD name="content_length" datatype="long" unit="byte"/>
      <DATA>
        <TABLEDATA>
          <TR><TD>obs-2</TD><TD>https://example.org/preview/obs-2.jpg</TD><TD></TD><TD></TD><TD>preview</TD><TD>#preview</TD><TD>image/jpeg</TD><TD>2000</TD></TR>
          <TR><TD>obs-2</TD><TD>https://example.org/data/obs-2.fits</TD><TD></TD><TD></TD><TD>primary</TD><TD>#this</TD><TD>application/fits</TD><TD>1024000</TD></TR>
          <TR><TD>obs-2</TD><TD>https://example.org/data/obs-2-b.fits</TD><TD></TD><TD></TD><TD>alt</TD><TD>#this</TD><TD>application/fits</TD><TD>512</TD></TR>
        </TABLEDATA>
      </DATA>
    </TABLE>
  </RESOURCE>
</VOTABLE>)";

TextFetcher FakeText(std::vector<std::pair<long, std::string>> responses, std::vector<std::string>* requested = nullptr) {
    auto call_count = std::make_shared<size_t>(0);
    return [responses = std::move(responses), requested, call_count](const std::string& url, HttpTextResult& result) {
        if (requested) {
            requested->push_back(url);
        }
        if (*call_count >= responses.size()) {
            result.error = "unexpected extra request";
            return false;
        }
        result.status = responses[*call_count].first;
        result.body = responses[*call_count].second;
        ++(*call_count);
        return true;
    };
}

CARTA::SiaQueryRequest QueryRequest() {
    CARTA::SiaQueryRequest request;
    request.set_service_url("https://example.org/sia/query");
    request.set_ra(10.68);
    request.set_dec(-41.27);
    request.set_radius(0.5);
    return request;
}

void AddFilter(CARTA::SiaQueryRequest& request, const std::string& key, const std::string& value) {
    auto* filter = request.add_filters();
    filter->set_key(key);
    filter->set_value(value);
}

} // namespace

TEST(SiaServices, ParsesNamedAndBareEntries) {
    auto services = ParseSiaServices({"GAVO|https://dc.g-vo.org/sia2", "https://example.org/sia/query", ""});
    ASSERT_EQ(services.size(), 2);
    EXPECT_EQ(services[0].name, "GAVO");
    EXPECT_EQ(services[0].query_url, "https://dc.g-vo.org/sia2");
    EXPECT_EQ(services[1].name, "example.org");
    EXPECT_EQ(services[1].query_url, "https://example.org/sia/query");
}

TEST(SiaQueryUrl, BuildsPosCircle) {
    std::string url, error_message;
    ASSERT_TRUE(BuildSiaQueryUrl(QueryRequest(), url, error_message));
    EXPECT_EQ(url, "https://example.org/sia/query?POS=CIRCLE%2010.68%20-41.27%200.5");
}

TEST(SiaQueryUrl, AppendsEscapedFilters) {
    auto request = QueryRequest();
    AddFilter(request, "BAND", "-Inf 0.21");
    AddFilter(request, "POL", "I");
    AddFilter(request, "POL", "Q");
    AddFilter(request, "MAXREC", "100");
    std::string url, error_message;
    ASSERT_TRUE(BuildSiaQueryUrl(request, url, error_message));
    EXPECT_NE(url.find("&BAND=-Inf%200.21"), std::string::npos);
    EXPECT_NE(url.find("&POL=I&POL=Q"), std::string::npos);
    EXPECT_NE(url.find("&MAXREC=100"), std::string::npos);
}

TEST(SiaQueryUrl, HandlesBaseUrlWithExistingQuery) {
    auto request = QueryRequest();
    request.set_service_url("https://example.org/sia?FORMAT=fits");
    std::string url, error_message;
    ASSERT_TRUE(BuildSiaQueryUrl(request, url, error_message));
    EXPECT_EQ(url.find("https://example.org/sia?FORMAT=fits&POS="), 0);
}

TEST(SiaQueryUrl, RejectsUnknownFilterKey) {
    auto request = QueryRequest();
    AddFilter(request, "UPLOAD", "gotcha");
    std::string url, error_message;
    EXPECT_FALSE(BuildSiaQueryUrl(request, url, error_message));
    EXPECT_NE(error_message.find("UPLOAD"), std::string::npos);
}

TEST(SiaQueryUrl, RejectsInvalidPosition) {
    auto request = QueryRequest();
    request.set_radius(0);
    std::string url, error_message;
    EXPECT_FALSE(BuildSiaQueryUrl(request, url, error_message));

    request = QueryRequest();
    request.set_ra(std::nan(""));
    EXPECT_FALSE(BuildSiaQueryUrl(request, url, error_message));
}

TEST(SiaQueryUrl, RejectsControlCharactersInFilterValue) {
    auto request = QueryRequest();
    AddFilter(request, "COLLECTION", "bad\nvalue");
    std::string url, error_message;
    EXPECT_FALSE(BuildSiaQueryUrl(request, url, error_message));
}

TEST(SiaRunQuery, MapsObscoreColumns) {
    std::vector<CARTA::SiaRecord> records;
    std::string error_message;
    ASSERT_TRUE(RunSiaQuery("https://example.org/sia/query?POS=x", records, error_message, FakeText({{200, kObscoreVotable}})));

    ASSERT_EQ(records.size(), 2);
    EXPECT_EQ(records[0].index(), 1);
    EXPECT_EQ(records[0].obs_id(), "obs-1");
    EXPECT_EQ(records[0].target_name(), "M31");
    EXPECT_EQ(records[0].collection(), "SURVEY");
    EXPECT_EQ(records[0].instrument(), "Cam1");
    EXPECT_EQ(records[0].dataproduct_type(), "image");
    EXPECT_EQ(records[0].calib_level(), 2);
    EXPECT_DOUBLE_EQ(records[0].ra(), 10.68);
    EXPECT_DOUBLE_EQ(records[0].dec(), 41.27);
    EXPECT_DOUBLE_EQ(records[0].fov(), 1.0);
    EXPECT_EQ(records[0].access_url(), "https://example.org/data/obs-1.fits");
    EXPECT_EQ(records[0].access_format(), "application/fits");
    EXPECT_EQ(records[0].access_estsize(), 2048);

    EXPECT_EQ(records[1].index(), 2);
    EXPECT_TRUE(std::isnan(records[1].fov()));
    EXPECT_EQ(records[1].access_estsize(), -1);
    EXPECT_EQ(records[1].access_url(), "https://example.org/datalink?ID=obs-2");
    EXPECT_TRUE(IsDataLinkFormat(records[1].access_format()));
    EXPECT_FALSE(IsDataLinkFormat(records[0].access_format()));
}

TEST(SiaRunQuery, SkipsProductsWhichAreNotImages) {
    std::vector<CARTA::SiaRecord> records;
    std::string error_message;
    ASSERT_TRUE(RunSiaQuery("https://example.org/q", records, error_message, FakeText({{200, kMixedProductVotable}})));

    ASSERT_EQ(records.size(), 3);
    EXPECT_EQ(records[0].obs_id(), "fits-product");
    EXPECT_EQ(records[1].obs_id(), "datalink-product");
    EXPECT_EQ(records[2].obs_id(), "unknown-format-product");
}

TEST(SiaRunQuery, NumbersRecordsByTheirPositionInTheKeptSet) {
    std::vector<CARTA::SiaRecord> records;
    std::string error_message;
    ASSERT_TRUE(RunSiaQuery("https://example.org/q", records, error_message, FakeText({{200, kMixedProductVotable}})));

    ASSERT_EQ(records.size(), 3);
    EXPECT_EQ(records[0].index(), 1);
    EXPECT_EQ(records[1].index(), 2);
    EXPECT_EQ(records[2].index(), 3);
    EXPECT_EQ(records[2].access_url(), "https://example.org/mystery");
}

TEST(SiaRunQuery, FallsBackToUcdColumns) {
    std::vector<CARTA::SiaRecord> records;
    std::string error_message;
    ASSERT_TRUE(RunSiaQuery("https://example.org/q", records, error_message, FakeText({{200, kUcdFallbackVotable}})));
    ASSERT_EQ(records.size(), 1);
    EXPECT_EQ(records[0].access_url(), "https://example.org/x.fits");
    EXPECT_EQ(records[0].access_format(), "image/fits");
    EXPECT_EQ(records[0].obs_id(), "obs-9");
}

TEST(SiaRunQuery, MapsDescriptiveFields) {
    std::vector<CARTA::SiaRecord> records;
    std::string error_message;
    ASSERT_TRUE(RunSiaQuery("https://example.org/q", records, error_message, FakeText({{200, kDescriptiveVotable}})));
    ASSERT_EQ(records.size(), 1);
    EXPECT_EQ(records[0].obs_title(), "Byurakan fbs1214 (IIF)");
    EXPECT_EQ(records[0].dataproduct_subtype(), "science");
    EXPECT_EQ(records[0].bandpass(), "IIF");
    EXPECT_TRUE(records[0].proposal_title().empty());
}

TEST(SiaRunQuery, MapsProposalAndAlternateBandpassNames) {
    std::vector<CARTA::SiaRecord> records;
    std::string error_message;
    ASSERT_TRUE(RunSiaQuery("https://example.org/q", records, error_message, FakeText({{200, kProposalVotable}})));
    ASSERT_EQ(records.size(), 1);
    EXPECT_EQ(records[0].proposal_title(), "The Hubble Constant");
    EXPECT_EQ(records[0].bandpass(), "IRAC1");
    EXPECT_TRUE(records[0].obs_title().empty());
}

TEST(SiaRunQuery, MatchesUcdsWhichCarryQualifiers) {
    std::vector<CARTA::SiaRecord> records;
    std::string error_message;
    ASSERT_TRUE(RunSiaQuery("https://example.org/q", records, error_message, FakeText({{200, kQualifiedUcdVotable}})));
    ASSERT_EQ(records.size(), 1);
    EXPECT_EQ(records[0].access_url(), "https://example.org/q.fits");
    EXPECT_DOUBLE_EQ(records[0].ra(), 10.68);
    EXPECT_DOUBLE_EQ(records[0].dec(), 41.27);
}

TEST(SiaRunQuery, RetriesBinarySerializationWithTabledata) {
    std::vector<CARTA::SiaRecord> records;
    std::string error_message;
    std::vector<std::string> requested;
    ASSERT_TRUE(RunSiaQuery(
        "https://example.org/q?POS=x", records, error_message, FakeText({{200, kBinaryVotable}, {200, kObscoreVotable}}, &requested)));
    ASSERT_EQ(requested.size(), 2);
    EXPECT_NE(requested[1].find("RESPONSEFORMAT=votable%2Ftd"), std::string::npos);
    EXPECT_EQ(records.size(), 2);
}

TEST(SiaRunQuery, SurfacesServiceErrorMessage) {
    std::vector<CARTA::SiaRecord> records;
    std::string error_message;
    EXPECT_FALSE(RunSiaQuery("https://example.org/q", records, error_message, FakeText({{200, kErrorVotable}})));
    EXPECT_NE(error_message.find("UsageFault"), std::string::npos);
}

TEST(SiaRunQuery, SurfacesErrorVotableWithHttpErrorStatus) {
    std::vector<CARTA::SiaRecord> records;
    std::string error_message;
    EXPECT_FALSE(RunSiaQuery("https://example.org/q", records, error_message, FakeText({{400, kErrorVotable}})));
    EXPECT_NE(error_message.find("UsageFault"), std::string::npos);
}

TEST(SiaRunQuery, EmptyTableIsSuccess) {
    std::vector<CARTA::SiaRecord> records;
    std::string error_message;
    EXPECT_TRUE(RunSiaQuery("https://example.org/q", records, error_message, FakeText({{200, kEmptyTableVotable}})));
    EXPECT_TRUE(records.empty());
}

TEST(SiaDataLink, PicksFirstThisEntry) {
    std::string file_url, error_message;
    ASSERT_TRUE(
        ResolveDataLinkFileUrl("https://example.org/datalink?ID=obs-2", file_url, error_message, FakeText({{200, kDataLinkVotable}})));
    EXPECT_EQ(file_url, "https://example.org/data/obs-2.fits");
}

TEST(SiaDataLink, FailsWhenNoLoadableEntry) {
    // Only non-#this rows
    const std::string previews_only = R"(<?xml version="1.0"?>
<VOTABLE xmlns="http://www.ivoa.net/xml/VOTable/v1.3"><RESOURCE type="results"><TABLE>
<FIELD name="access_url" datatype="char" arraysize="*"/><FIELD name="semantics" datatype="char" arraysize="*"/><FIELD name="content_type" datatype="char" arraysize="*"/>
<DATA><TABLEDATA><TR><TD>https://example.org/p.jpg</TD><TD>#preview</TD><TD>image/jpeg</TD></TR></TABLEDATA></DATA>
</TABLE></RESOURCE></VOTABLE>)";
    std::string file_url, error_message;
    EXPECT_FALSE(ResolveDataLinkFileUrl("https://example.org/datalink?ID=x", file_url, error_message, FakeText({{200, previews_only}})));
    EXPECT_FALSE(error_message.empty());
}

namespace {

SizeProbeFetcher FakeSizeProbe(std::map<std::string, SizeProbeResult> responses, std::vector<std::string>* requested = nullptr) {
    return [responses = std::move(responses), requested](const std::string& url, SizeProbeResult& result) {
        if (requested) {
            requested->push_back(url);
        }
        auto entry = responses.find(url);
        if (entry == responses.end()) {
            result.error = "unexpected url " + url;
            return false;
        }
        result = entry->second;
        return true;
    };
}

SizeProbeResult FitsProbe(int64_t size) {
    SizeProbeResult result;
    result.status = 200;
    result.content_length = size;
    result.content_type = "application/fits";
    return result;
}

SizeProbeResult DataLinkProbe() {
    SizeProbeResult result;
    result.status = 200;
    result.content_length = -1;
    result.content_type = "application/x-votable+xml;content=datalink";
    return result;
}

} // namespace

TEST(SiaResolveFileUrl, DirectFitsUrlPassesThrough) {
    std::string file_url, error_message;
    auto probe = FakeSizeProbe({{"https://example.org/x.fits", FitsProbe(1000)}});
    EXPECT_TRUE(ResolveSiaFileUrl("https://example.org/x.fits", false, 2000, file_url, error_message, FakeText({}), probe));
    EXPECT_EQ(file_url, "https://example.org/x.fits");
}

TEST(SiaResolveFileUrl, ResolvesDeclaredDataLinkRecord) {
    std::string file_url, error_message;
    auto probe = FakeSizeProbe({{"https://example.org/data/obs-2.fits", FitsProbe(1000)}});
    EXPECT_TRUE(ResolveSiaFileUrl(
        "https://example.org/datalink?ID=obs-2", true, 2000, file_url, error_message, FakeText({{200, kDataLinkVotable}}), probe));
    EXPECT_EQ(file_url, "https://example.org/data/obs-2.fits");
}

TEST(SiaResolveFileUrl, ResolvesUrlThatRedirectsToDataLink) {
    // e.g. GAVO's getproduct URLs redirect to a DataLink document despite declaring a FITS format
    std::string file_url, error_message;
    auto probe = FakeSizeProbe({
        {"https://example.org/getproduct/x", DataLinkProbe()},
        {"https://example.org/data/obs-2.fits", FitsProbe(1000)},
    });
    EXPECT_TRUE(ResolveSiaFileUrl(
        "https://example.org/getproduct/x", false, 2000, file_url, error_message, FakeText({{200, kDataLinkVotable}}), probe));
    EXPECT_EQ(file_url, "https://example.org/data/obs-2.fits");
}

TEST(SiaResolveFileUrl, RejectsFileOverSizeLimit) {
    std::string file_url, error_message;
    auto probe = FakeSizeProbe({{"https://example.org/x.fits", FitsProbe(5000)}});
    EXPECT_FALSE(ResolveSiaFileUrl("https://example.org/x.fits", false, 2000, file_url, error_message, FakeText({}), probe));
    EXPECT_NE(error_message.find("size"), std::string::npos);
}

TEST(SiaResolveFileUrl, RejectsRepeatedDataLinkIndirection) {
    std::string file_url, error_message;
    auto probe = FakeSizeProbe({
        {"https://example.org/a", DataLinkProbe()},
        {"https://example.org/data/obs-2.fits", DataLinkProbe()},
    });
    EXPECT_FALSE(ResolveSiaFileUrl("https://example.org/a", false, 2000, file_url, error_message,
        FakeText({{200, kDataLinkVotable}, {200, kDataLinkVotable}}), probe));
}

TEST(SiaResolveFileUrl, RejectsInvalidUrl) {
    std::string file_url, error_message;
    EXPECT_FALSE(ResolveSiaFileUrl("ftp://example.org/x.fits", false, 2000, file_url, error_message, FakeText({}), FakeSizeProbe({})));
}
