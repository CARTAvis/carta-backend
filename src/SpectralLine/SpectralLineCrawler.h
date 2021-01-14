/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_BACKEND_SPECTRAL_LINE_CRAWLER_H_
#define CARTA_BACKEND_SPECTRAL_LINE_CRAWLER_H_

#include <carta-protobuf/spectral_line_request.pb.h>
#include <string>

namespace carta {
class SpectralLineCrawler {
public:
    SpectralLineCrawler();
    ~SpectralLineCrawler();
    static void SendRequest(const CARTA::DoubleBounds& frequencyRange, const double line_intensity_lower_limit,
        CARTA::SpectralLineResponse& spectral_line_response);

private:
    static const std::string Headers[];
    static size_t WriteMemoryCallback(void* contents, size_t size, size_t nmemb, void* userp);
    static void ParseQueryResult(const std::string& results, CARTA::SpectralLineResponse& spectral_line_response);
};
} // namespace carta

#endif // CARTA_BACKEND_SPECTRAL_LINE_CRAWLER_H_
