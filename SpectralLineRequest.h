#ifndef CARTA_BACKEND_SPECTRAL_LINE_REQUEST_H_
#define CARTA_BACKEND_SPECTRAL_LINE_REQUEST_H_

#include <string>
#include <carta-protobuf/spectral_line_request.pb.h>

namespace carta {
    struct MemoryStruct {
        char *memory;
        size_t size;
    };

    class SpectralLineRequest {
        public:
        SpectralLineRequest();
        ~SpectralLineRequest();
        static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp);
        static void SendRequest(CARTA::DoubleBounds frequencyRange);

        private:
        static const std::string SplatalogueURL;
        static void parsingQueryResult(MemoryStruct& results);
    };
}

#endif // CARTA_BACKEND_SPECTRAL_LINE_REQUEST_H_
