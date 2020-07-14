#ifndef CARTA_BACKEND_SPECTRAL_LINE_REQUEST_H_
#define CARTA_BACKEND_SPECTRAL_LINE_REQUEST_H_

#include <carta-protobuf/spectral_line_request.pb.h>
#include <string>

namespace carta {
struct MemoryStruct {
    char *memory;
    size_t size;
};
class SpectralLineRequest {
    public:
    SpectralLineRequest();
    ~SpectralLineRequest();
    void SendRequest(const CARTA::DoubleBounds& frequencyRange, CARTA::SpectralLineResponse& spectral_line_response);
    private:
    static const std::string SplatalogueURL;
    static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp);
    static void ParsingQueryResult(const MemoryStruct& results, CARTA::SpectralLineResponse& spectral_line_response);
};
}

#endif // CARTA_BACKEND_SPECTRAL_LINE_REQUEST_H_
