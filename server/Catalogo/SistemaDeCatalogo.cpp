//
// Created by nacho on 6/2/2026.
//

#include "SistemaDeCatalogo.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <cstring>

namespace fs = std::filesystem;

// --------------------------------------------------------------------------
// Constructor y carga inicial
// --------------------------------------------------------------------------

SistemaDeCatalogo::SistemaDeCatalogo(const std::string& path) : catalogPath(path) {
    fs::create_directories(path);
}

void SistemaDeCatalogo::load() {
    // Limpiar el estado en memoria antes de cargar para evitar duplicados
    databases.clear();
    tables.clear();
    indexes.clear();

    // Cargar bases de datos activas
    for (const auto& rec : readAllRecords<DbRecord>(dbsFilePath())) {
        if (rec.active) databases.push_back(rec.name);
    }

    // Cargar columnas primero porque las necesitamos para armar las TableDefinitions
    // Mapa temporal: "dbName.tableName" -> lista de ColumnDefinition (sin ordenar aun)
    std::unordered_map<std::string, std::vector<ColumnDefinition>> colMap;
    for (const auto& rec : readAllRecords<ColumnRecord>(columnsFilePath())) {
        if (!rec.active) continue;

        ColumnDefinition col;
        col.name        = rec.colName;
        col.type        = uintToType(rec.type);
        col.varcharSize = rec.varcharSize;
        col.nullable    = rec.nullable;
        col.colIndex    = rec.colIndex;

        std::string key = tableKey(rec.dbName, rec.tableName);
        colMap[key].push_back(col);
    }

    // Ordenar cada lista de columnas por su indice para reconstruir el orden original
    for (auto& [key, cols] : colMap) {
        std::sort(cols.begin(), cols.end(), [](const ColumnDefinition& a, const ColumnDefinition& b) {
            return a.colIndex < b.colIndex;
        });
    }

    // Cargar tablas activas y asociarles sus columnas ya ordenadas
    for (const auto& rec : readAllRecords<TableRecord>(tablesFilePath())) {
        if (!rec.active) continue;

        TableDefinition def;
        def.dbName    = rec.dbName;
        def.tableName = rec.tableName;

        std::string key = tableKey(rec.dbName, rec.tableName);
        if (colMap.count(key)) {
            def.columns = colMap[key];
        }

        tables[key] = def;
    }

    // Cargar indices activos
    for (const auto& rec : readAllRecords<IndexRecord>(indexesFilePath())) {
        if (!rec.active) continue;

        IndexDefinition idx;
        idx.indexName = rec.indexName;
        idx.tableName = rec.tableName;
        idx.dbName    = rec.dbName;
        idx.colName   = rec.colName;
        idx.type      = rec.type;
        indexes.push_back(idx);
    }
}

// --------------------------------------------------------------------------
// Rutas a los archivos del catalogo
// --------------------------------------------------------------------------

std::string SistemaDeCatalogo::tableKey(const std::string& dbName, const std::string& tableName) const {
    return dbName + "." + tableName;
}

std::string SistemaDeCatalogo::dbsFilePath()     const { return catalogPath + "/SystemDatabases.bin"; }
std::string SistemaDeCatalogo::tablesFilePath()  const { return catalogPath + "/SystemTables.bin"; }
std::string SistemaDeCatalogo::columnsFilePath() const { return catalogPath + "/SystemColumns.bin"; }
std::string SistemaDeCatalogo::indexesFilePath() const { return catalogPath + "/SystemIndexes.bin"; }

// --------------------------------------------------------------------------
// Operaciones sobre el disco (templates)
// --------------------------------------------------------------------------

template<typename T>
void SistemaDeCatalogo::appendRecord(const std::string& filePath, const T& record) const {
    std::ofstream file(filePath, std::ios::binary | std::ios::app);
    if (!file) throw std::runtime_error("No se pudo abrir el archivo de catalogo: " + filePath);
    file.write(reinterpret_cast<const char*>(&record), sizeof(T));
}

template<typename T>
std::vector<T> SistemaDeCatalogo::readAllRecords(const std::string& filePath) const {
    std::vector<T> result;
    std::ifstream file(filePath, std::ios::binary);
    if (!file) return result; // El archivo puede no existir la primera vez

    T record;
    while (file.read(reinterpret_cast<char*>(&record), sizeof(T))) {
        result.push_back(record);
    }
    return result;
}

template<typename T, typename Predicate>
bool SistemaDeCatalogo::deactivateRecord(const std::string& filePath, Predicate pred) const {
    std::fstream file(filePath, std::ios::binary | std::ios::in | std::ios::out);
    if (!file) return false;

    T record;
    std::streampos pos = 0;

    while (file.read(reinterpret_cast<char*>(&record), sizeof(T))) {
        if (record.active && pred(record)) {
            // Volver atras y sobreescribir solo el campo active
            file.seekp(pos);
            record.active = 0;
            file.write(reinterpret_cast<char*>(&record), sizeof(T));
            return true;
        }
        pos = file.tellg();
    }
    return false;
}

// --------------------------------------------------------------------------
// Conversiones de tipo
// --------------------------------------------------------------------------

uint8_t SistemaDeCatalogo::typeToUint(ColumnType t) {
    switch (t) {
        case ColumnType::INTEGER:  return 0;
        case ColumnType::DOUBLE:   return 1;
        case ColumnType::VARCHAR:  return 2;
        case ColumnType::DATETIME: return 3;
        default: return 0;
    }
}

ColumnType SistemaDeCatalogo::uintToType(uint8_t v) {
    switch (v) {
        case 0: return ColumnType::INTEGER;
        case 1: return ColumnType::DOUBLE;
        case 2: return ColumnType::VARCHAR;
        case 3: return ColumnType::DATETIME;
        default: return ColumnType::INTEGER;
    }
}

// --------------------------------------------------------------------------
// Bases de datos
// --------------------------------------------------------------------------

bool SistemaDeCatalogo::createDatabase(const std::string& name) {
    if (databaseExists(name)) return false;

    // Escribir al disco
    DbRecord rec = {};
    std::strncpy(rec.name, name.c_str(), 63);
    rec.active = 1;
    appendRecord(dbsFilePath(), rec);

    // Actualizar memoria
    databases.push_back(name);
    return true;
}

bool SistemaDeCatalogo::databaseExists(const std::string& name) const {
    for (const auto& db : databases) {
        if (db == name) return true;
    }
    return false;
}

std::vector<std::string> SistemaDeCatalogo::listDatabases() const {
    return databases;
}

// --------------------------------------------------------------------------
// Tablas
// --------------------------------------------------------------------------

bool SistemaDeCatalogo::createTable(const TableDefinition& tableDef) {
    std::string key = tableKey(tableDef.dbName, tableDef.tableName);
    if (tables.count(key)) return false;

    // Persistir el registro de la tabla
    TableRecord trec = {};
    std::strncpy(trec.tableName, tableDef.tableName.c_str(), 63);
    std::strncpy(trec.dbName,    tableDef.dbName.c_str(),    63);
    trec.active = 1;
    appendRecord(tablesFilePath(), trec);

    // Persistir cada columna
    for (uint32_t i = 0; i < tableDef.columns.size(); i++) {
        const ColumnDefinition& col = tableDef.columns[i];
        ColumnRecord crec = {};
        std::strncpy(crec.tableName, tableDef.tableName.c_str(), 63);
        std::strncpy(crec.dbName,    tableDef.dbName.c_str(),    63);
        std::strncpy(crec.colName,   col.name.c_str(),           63);
        crec.type        = typeToUint(col.type);
        crec.varcharSize = col.varcharSize;
        crec.nullable    = col.nullable ? 1 : 0;
        crec.colIndex    = i;
        crec.active      = 1;
        appendRecord(columnsFilePath(), crec);
    }

    // Actualizar memoria
    tables[key] = tableDef;
    return true;
}

bool SistemaDeCatalogo::tableExists(const std::string& dbName, const std::string& tableName) const {
    return tables.count(tableKey(dbName, tableName)) > 0;
}

TableDefinition SistemaDeCatalogo::getTableDef(const std::string& dbName, const std::string& tableName) const {
    std::string key = tableKey(dbName, tableName);
    if (!tables.count(key)) {
        throw std::runtime_error("La tabla no existe: " + key);
    }
    return tables.at(key);
}

bool SistemaDeCatalogo::dropTable(const std::string& dbName, const std::string& tableName) {
    std::string key = tableKey(dbName, tableName);
    if (!tables.count(key)) return false;

    // Marcar como inactivo en SystemTables
    deactivateRecord<TableRecord>(tablesFilePath(), [&](const TableRecord& r) {
        return std::string(r.tableName) == tableName && std::string(r.dbName) == dbName;
    });

    // Marcar todas sus columnas como inactivas en SystemColumns
    auto colRecords = readAllRecords<ColumnRecord>(columnsFilePath());
    std::fstream colFile(columnsFilePath(), std::ios::binary | std::ios::in | std::ios::out);
    if (colFile) {
        ColumnRecord rec;
        std::streampos pos = 0;
        while (colFile.read(reinterpret_cast<char*>(&rec), sizeof(rec))) {
            if (rec.active &&
                std::string(rec.tableName) == tableName &&
                std::string(rec.dbName)    == dbName) {
                colFile.seekp(pos);
                rec.active = 0;
                colFile.write(reinterpret_cast<char*>(&rec), sizeof(rec));
            }
            pos = colFile.tellg();
        }
    }

    // Remover de memoria
    tables.erase(key);
    return true;
}

std::vector<std::string> SistemaDeCatalogo::listTables(const std::string& dbName) const {
    std::vector<std::string> result;
    for (const auto& [key, def] : tables) {
        if (def.dbName == dbName) result.push_back(def.tableName);
    }
    return result;
}

// --------------------------------------------------------------------------
// Indices
// --------------------------------------------------------------------------

bool SistemaDeCatalogo::addIndex(const IndexDefinition& idx) {
    if (indexExists(idx.tableName, idx.colName)) return false;

    IndexRecord rec = {};
    std::strncpy(rec.indexName, idx.indexName.c_str(), 63);
    std::strncpy(rec.tableName, idx.tableName.c_str(), 63);
    std::strncpy(rec.dbName,    idx.dbName.c_str(),    63);
    std::strncpy(rec.colName,   idx.colName.c_str(),   63);
    std::strncpy(rec.type,      idx.type.c_str(),       7);
    rec.active = 1;
    appendRecord(indexesFilePath(), rec);

    indexes.push_back(idx);
    return true;
}

bool SistemaDeCatalogo::indexExists(const std::string& tableName, const std::string& colName) const {
    for (const auto& idx : indexes) {
        if (idx.tableName == tableName && idx.colName == colName) return true;
    }
    return false;
}

IndexDefinition SistemaDeCatalogo::getIndex(const std::string& tableName, const std::string& colName) const {
    for (const auto& idx : indexes) {
        if (idx.tableName == tableName && idx.colName == colName) return idx;
    }
    throw std::runtime_error("Indice no encontrado para la columna: " + colName);
}

std::vector<IndexDefinition> SistemaDeCatalogo::listIndexes() const {
    return indexes;
}

bool SistemaDeCatalogo::dropIndex(const std::string& indexName) {
    bool found = deactivateRecord<IndexRecord>(indexesFilePath(), [&](const IndexRecord& r) {
        return std::string(r.indexName) == indexName;
    });

    if (found) {
        indexes.erase(std::remove_if(indexes.begin(), indexes.end(),
            [&](const IndexDefinition& idx) { return idx.indexName == indexName; }),
            indexes.end());
    }

    return found;
}