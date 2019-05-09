#ifndef CARTA_BACKEND__EVENTHEADER_H_
#define CARTA_BACKEND__EVENTHEADER_H_

namespace CARTA {
const uint16_t ICD_VERSION = 2;
struct EventHeader {
    uint16_t _type;
    uint16_t _icd_vers;
    uint32_t _req_id;
};
} // namespace CARTA

#endif // CARTA_BACKEND__EVENTHEADER_H_
