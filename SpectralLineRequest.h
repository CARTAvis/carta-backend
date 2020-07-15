#ifndef CARTA_BACKEND_SPECTRAL_LINE_REQUEST_H_
#define CARTA_BACKEND_SPECTRAL_LINE_REQUEST_H_

#include <carta-protobuf/spectral_line_request.pb.h>
#include <string>

namespace carta {
class SpectralLineRequest {
public:
    SpectralLineRequest();
    ~SpectralLineRequest();
    void SendRequest(const CARTA::DoubleBounds& frequencyRange, CARTA::SpectralLineResponse& spectral_line_response);

private:
    static const std::string SplatalogueURL;
    static size_t WriteMemoryCallback(void* contents, size_t size, size_t nmemb, void* userp);
    static void ParseQueryResult(const std::string& results, CARTA::SpectralLineResponse& spectral_line_response);
};
} // namespace carta

#endif // CARTA_BACKEND_SPECTRAL_LINE_REQUEST_H_
