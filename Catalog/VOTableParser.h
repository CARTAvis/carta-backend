#ifndef CARTA_BACKEND__VOTABLEPARSER_H_
#define CARTA_BACKEND__VOTABLEPARSER_H_

#include <libxml/xmlreader.h>

#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "VOTableCarrier.h"

namespace catalog {

class VOTableParser {
    enum ElementName {
        VOTABLE = 0,
        RESOURCE = 1,
        DESCRIPTION = 2,
        DEFINITIONS = 3,
        INFO = 4,
        PARAM = 5,
        TABLE = 6,
        FIELD = 7,
        GROUP = 8,
        FIELDref = 9,
        PARAMref = 10,
        VALUES = 11,
        MIN = 12,
        MAX = 13,
        OPTION = 14,
        LINK = 15,
        DATA = 16,
        TABLEDATA = 17,
        TD = 18,
        TR = 19,
        FITS = 20,
        BINARY = 21,
        STREAM = 22,
        COOSYS = 23,
        NONE = 24
    };

public:
    VOTableParser(std::string filename, VOTableCarrier* carrier, bool only_read_to_header = false, bool verbose = false);
    ~VOTableParser();

    static bool IsVOTable(std::string filename);

private:
    static void CleanupParser();
    void Scan();
    void Parse();
    void Print(std::string name, std::string value);
    static ElementName GetElementName(std::string name);
    void IncreaseElementCounts(ElementName element_name);
    void FillElementAttributes(ElementName element_name, std::string name, std::string value);
    void FillElementValues(ElementName element_name, std::string value);

    bool _verbose;
    xmlTextReaderPtr _reader;
    ElementName _element_name = NONE;     // Current element name
    ElementName _pre_element_name = NONE; // Previous element name
    VOTableCarrier* _carrier;             // Store the VOTable skimming results
    int _coosys_counts = 0;               // Element <COOSYS> counts (1 ~)
    int _field_counts = 0;                // Element <FIELD> counts (1 ~)
    int _tr_counts = 0;                   // Element <TR> counts (1 ~)
    int _td_counts = 0;                   // Element <TD> counts (1 ~ _field_counts)
    bool _td_filled = false;
    bool _continue_read = true;
    bool _only_read_to_header;
};

} // namespace catalog

#endif // CARTA_BACKEND__VOTABLEPARSER_H_