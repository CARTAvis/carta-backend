#ifndef VOTABLE_TEST__TABLE_H_
#define VOTABLE_TEST__TABLE_H_

#include <string>
#include <unordered_map>
#include <vector>

#include "Columns.h"
#include "TableView.h"

#define MAX_HEADER_SIZE (64 * 1024)
// Valid for little-endian only
#define XML_MAGIC_NUMBER 0x6D783F3C
#define FITS_MAGIC_NUMBER 0x504D4953

namespace carta {

class TableView;

class Table {
public:
    Table(const std::string& filename, bool header_only = false);
    bool IsValid() const;
    void PrintInfo(bool skip_unknowns = true) const;
    const Column* GetColumnByName(const std::string& name) const;
    const Column* GetColumnById(const std::string& id) const;
    size_t NumColumns() const;
    size_t NumRows() const;
    const std::string& Description() const;
    TableView View() const;

    const Column* operator[](size_t i) const;
    const Column* operator[](const std::string& name_or_id) const;

protected:
    void ConstructFromXML(bool header_only = false);
    bool PopulateFields(const pugi::xml_node& table);
    bool PopulateRows(const pugi::xml_node& table);

    bool ConstructFromFITS(bool header_only = false);

    bool _valid;
    int64_t _num_rows;
    std::string _filename;
    std::string _description;
    std::vector<std::unique_ptr<Column>> _columns;
    std::unordered_map<std::string, Column*> _column_name_map;
    std::unordered_map<std::string, Column*> _column_id_map;
    static std::string GetHeader(const std::string& filename);
    static uint32_t GetMagicNumber(const std::string& filename);
};
} // namespace carta
#endif // VOTABLE_TEST__TABLE_H_
