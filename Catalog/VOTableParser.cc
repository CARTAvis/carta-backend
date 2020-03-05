#include "VOTableParser.h"

using namespace catalog;

const std::unordered_map<std::string, VOTableParser::ElementName> VOTableParser::elementEnumMap = {
    {"VOTABLE", VOTableParser::ElementName::VOTABLE}, {"RESOURCE", VOTableParser::ElementName::RESOURCE},
    {"DESCRIPTION", VOTableParser::ElementName::DESCRIPTION}, {"DEFINITIONS", VOTableParser::ElementName::DEFINITIONS},
    {"INFO", VOTableParser::ElementName::INFO}, {"PARAM", VOTableParser::ElementName::PARAM}, {"TABLE", VOTableParser::ElementName::TABLE},
    {"FIELD", VOTableParser::ElementName::FIELD}, {"GROUP", VOTableParser::ElementName::GROUP},
    {"FIELDref", VOTableParser::ElementName::FIELDref}, {"PARAMref", VOTableParser::ElementName::PARAMref},
    {"VALUES", VOTableParser::ElementName::VALUES}, {"MIN", VOTableParser::ElementName::MIN}, {"MAX", VOTableParser::ElementName::MAX},
    {"OPTION", VOTableParser::ElementName::OPTION}, {"LINK", VOTableParser::ElementName::LINK}, {"DATA", VOTableParser::ElementName::DATA},
    {"TABLEDATA", VOTableParser::ElementName::TABLEDATA}, {"TD", VOTableParser::ElementName::TD}, {"TR", VOTableParser::ElementName::TR},
    {"FITS", VOTableParser::ElementName::FITS}, {"BINARY", VOTableParser::ElementName::BINARY},
    {"BINARY2", VOTableParser::ElementName::BINARY2}, {"STREAM", VOTableParser::ElementName::STREAM},
    {"COOSYS", VOTableParser::ElementName::COOSYS}};

VOTableParser::VOTableParser(std::string filename, VOTableCarrier* carrier, bool only_read_to_header, bool verbose)
    : _carrier(carrier), _only_read_to_header(only_read_to_header), _verbose(verbose) {
    if (!IsVOTable(filename)) {
        std::cerr << "File: " << filename << " is NOT a VOTable!" << std::endl;
        _continue_read = false;
        return;
    }
    _reader = xmlReaderForFile(filename.c_str(), NULL, 0);
    if (_reader == nullptr) {
        std::cerr << "Unable to open " << filename << std::endl;
        return;
    }
    if (_carrier) {
        _carrier->SetFileName(filename);
    }
    Scan(); // Scan the VOTable
}

VOTableParser::~VOTableParser() {
    xmlFreeTextReader(_reader);
    CleanupParser();
}

bool VOTableParser::IsVOTable(std::string filename) {
    xmlTextReaderPtr reader;
    xmlChar* name_xmlchar;
    std::string name;
    reader = xmlReaderForFile(filename.c_str(), NULL, 0);
    if (xmlTextReaderRead(reader)) {
        if (xmlTextReaderNodeType(reader) == XML_READER_TYPE_ELEMENT) {
            name_xmlchar = xmlTextReaderName(reader);
            name = (char*)name_xmlchar;
            if (GetElementName(name) == ElementName::VOTABLE) {
                xmlFreeTextReader(reader);
                CleanupParser();
                return true;
            }
        }
    }
    xmlFreeTextReader(reader);
    CleanupParser();
    return false;
}

void VOTableParser::CleanupParser() {
    // Cleanup function for the XML library.
    xmlCleanupParser();

    // This is to debug memory for regression tests
    xmlMemoryDump();
}

void VOTableParser::Scan() {
    int result;
    do {
        // 1 if the node was read successfully, 0 if there is no more nodes to read, or -1 in case of error
        result = xmlTextReaderRead(_reader);
        if (result == 1 && _continue_read) {
            Parse(); // Parse the VOTable and store data in the VOTableCarrier
        } else {
            if (_verbose) {
                std::cout << "End of the XML file." << std::endl;
            }
            break;
        }
    } while (true);
}

void VOTableParser::Parse() {
    int node_type = xmlTextReaderNodeType(_reader);
    xmlChar* name_xmlchar;
    xmlChar* value_xmlchar;
    name_xmlchar = xmlTextReaderName(_reader);
    if (name_xmlchar == NULL) {
        name_xmlchar = xmlStrdup(BAD_CAST "--");
    }
    value_xmlchar = xmlTextReaderValue(_reader);

    std::string name;
    std::string value;
    name = (char*)name_xmlchar;
    xmlFree(name_xmlchar); // Not sure if it is necessary, just for safety.
    if (value_xmlchar) {
        value = (char*)value_xmlchar;
        xmlFree(value_xmlchar); // Not sure if it is necessary, just for safety.
    }

    switch (node_type) {
        case XML_READER_TYPE_ELEMENT:
            _pre_element_name = _element_name;
            _element_name = GetElementName(name);
            if (_only_read_to_header && _element_name == ElementName::DATA) {
                _continue_read = false;
                break;
            }
            IncreaseElementCounts(_element_name);
            // Loop through the attributes
            while (xmlTextReaderMoveToNextAttribute(_reader)) {
                Parse();
            }
            break;
        case XML_READER_TYPE_END_ELEMENT:
            if (_element_name == ElementName::TD && !_td_filled && _carrier) {
                // Fill the TR element values as "" if there is an empty column, i.e. <TD></TD>.
                _carrier->FillTdValues(_td_counts, "");
                _td_filled = true; // Decrease the TD counter in order to mark such TR element has been filled
            }
            break;
        case XML_READER_TYPE_ATTRIBUTE:
            FillElementAttributes(_element_name, name, value);
            break;
        case XML_READER_TYPE_TEXT:
            FillElementValues(_element_name, value);
            break;

            // Regardless the following node types
        case XML_READER_TYPE_NONE:
            break;
        case XML_READER_TYPE_SIGNIFICANT_WHITESPACE:
            break;
        case XML_READER_TYPE_WHITESPACE:
            break;
        case XML_READER_TYPE_CDATA:
            break;
        case XML_READER_TYPE_ENTITY_REFERENCE:
            break;
        case XML_READER_TYPE_ENTITY:
            break;
        case XML_READER_TYPE_PROCESSING_INSTRUCTION:
            break;
        case XML_READER_TYPE_COMMENT:
            break;
        case XML_READER_TYPE_DOCUMENT:
            break;
        case XML_READER_TYPE_DOCUMENT_TYPE:
            break;
        case XML_READER_TYPE_DOCUMENT_FRAGMENT:
            break;
        case XML_READER_TYPE_NOTATION:
            break;

        default:
            std::cerr << "Fail to parse the XML text!" << std::endl;
    }
}

VOTableParser::ElementName VOTableParser::GetElementName(std::string name) {
    auto itr = elementEnumMap.find(name);
    if (itr == elementEnumMap.end()) {
        return ElementName::NONE;
    }
    return itr->second;
}

void VOTableParser::IncreaseElementCounts(ElementName element_name) {
    switch (element_name) {
        case ElementName::COOSYS:
            ++_coosys_counts;
            break;
        case ElementName::FIELD:
            ++_field_counts;
            break;
        case ElementName::TR:
            ++_tr_counts;
            break;
        case ElementName::TD:
            if (_field_counts > 0) {
                _td_counts = (((_td_counts + 1) % _field_counts) == 0) ? _field_counts : ((_td_counts + 1) % _field_counts);
            } else {
                std::cerr << "There is no column header!" << std::endl;
            }
            _td_filled = false;
            break;
        default:; // Do not count any elements
    }
}

void VOTableParser::FillElementAttributes(ElementName element_name, std::string name, std::string value) {
    if (!_carrier) {
        std::cerr << "The VOTableCarrier pointer is null!" << std::endl;
        return;
    }
    switch (element_name) {
        case ElementName::VOTABLE:
            _carrier->FillVOTableAttributes(name, value);
            break;
        case ElementName::COOSYS:
            _carrier->FillCoosysAttributes(_coosys_counts, name, value);
            break;
        case ElementName::FIELD:
            _carrier->FillFieldAttributes(_field_counts, name, value);
            break;
        default:; // Do not fill any attributes
    }
}

void VOTableParser::FillElementValues(ElementName element_name, std::string value) {
    if (!_carrier) {
        std::cerr << "The VOTableCarrier pointer is null!" << std::endl;
        return;
    }
    switch (element_name) {
        case ElementName::DESCRIPTION:
            if (_pre_element_name == ElementName::FIELD) {
                _carrier->FillFieldDescriptions(_field_counts, value);
            } else {
                _carrier->FillFileDescription(value);
            }
            break;
        case ElementName::TD:
            if (!_td_filled) {
                _carrier->FillTdValues(_td_counts, value);
                _td_filled = true;
            }
            break;
        default:; // Do not fill any values
    }
}