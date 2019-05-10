#ifndef CARTA_BACKEND__EVENTHEADER_H_
#define CARTA_BACKEND__EVENTHEADER_H_

namespace CARTA {
const uint16_t ICD_VERSION = 2;
struct EventHeader {
    uint16_t type;
    uint16_t icd_version;
    uint32_t request_id;
};
} // namespace CARTA

#endif // CARTA_BACKEND__EVENTHEADER_H_
