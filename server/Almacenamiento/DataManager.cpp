//
// Created by nacho on 6/2/2026.
//

#include "DataManager.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <cstring>
#include <ctime>
#include <sstream>
#include <iomanip>

namespace fs = std::filesystem;

// Clave usada para el cifrado XOR. No cambiar una vez que existan datos en disco,
// porque los archivos existentes quedarian ilegibles.
static const char   XOR_KEY[]     = "TinySQL2026";
static const size_t XOR_KEY_LEN   = sizeof(XOR_KEY) - 1; // Sin el terminador nulo

// --------------------------------------------------------------------------
// Constructor
// --------------------------------------------------------------------------

DataManager::DataManager(const std::string& dataRootPath) : dataRoot(dataRootPath) {
    // Crear la carpeta raiz si todavia no existe
    fs::create_directories(dataRootPath);
}

// --------------------------------------------------------------------------
// Rutas y helpers de bajo nivel
// --------------------------------------------------------------------------

std::string DataManager::tablePath(const std::string& dbName, const std::string& tableName) const {
    // Estructura: data/NombreDB/NombreTabla.bin
    return dataRoot + "/" + dbName + "/" + tableName + ".bin";
}

int64_t DataManager::recordOffset(const TableDefinition& tableDef, uint32_t index) const {
    // El primer registro empieza justo despues del header de 4 bytes
    return HEADER_SIZE + static_cast<int64_t>(index) * tableDef.recordSize();
}

uint32_t DataManager::readTotalCount(const std::string& path) const {
    std::ifstream file(path, std::ios::binary);
    if (!file) return 0;
    uint32_t count = 0;
    file.read(reinterpret_cast<char*>(&count), sizeof(count));
    return count;
}

void DataManager::writeTotalCount(const std::string& path, uint32_t count) const {
    // Abrir en modo lectura+escritura para no destruir el contenido existente
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file) return;
    file.seekp(0, std::ios::beg);
    file.write(reinterpret_cast<char*>(&count), sizeof(count));
}

// --------------------------------------------------------------------------
// Cifrado XOR
// --------------------------------------------------------------------------

void DataManager::xorCipher(std::vector<char>& data) const {
    // XOR es simetrico: aplicarlo dos veces sobre los mismos datos los deja como estaban.
    // Cifrar == Descifrar con la misma clave.
    for (size_t i = 0; i < data.size(); i++) {
        data[i] ^= XOR_KEY[i % XOR_KEY_LEN];
    }
}

// --------------------------------------------------------------------------
// Manejo de fechas
// --------------------------------------------------------------------------

int64_t DataManager::parseDateTime(const std::string& dt) const {
    // Convierte "YYYY-MM-DD HH:MM:SS" a segundos desde epoch (1970-01-01)
    std::tm t = {};
    std::istringstream ss(dt);
    ss >> std::get_time(&t, "%Y-%m-%d %H:%M:%S");
    if (ss.fail()) return 0;
    t.tm_isdst = -1; // Dejar que el sistema determine si hay horario de verano
    return static_cast<int64_t>(std::mktime(&t));
}

std::string DataManager::formatDateTime(int64_t timestamp) const {
    std::time_t t = static_cast<std::time_t>(timestamp);
    std::tm* tm_info = std::localtime(&t);
    if (!tm_info) return "0000-00-00 00:00:00";
    std::ostringstream ss;
    ss << std::put_time(tm_info, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

// --------------------------------------------------------------------------
// Serializacion y deserializacion
// --------------------------------------------------------------------------

std::vector<char> DataManager::serialize(const TableDefinition& tableDef, const Row& record) const {
    // El buffer tiene exactamente recordSize() bytes.
    // Posicion 0: deleted flag (activo por defecto).
    // Posiciones 1..N: columnas en orden de definicion.
    std::vector<char> buffer(tableDef.recordSize(), 0);
    buffer[0] = FLAG_ACTIVE;

    size_t pos = 1; // Avanzamos columna por columna despues del flag

    for (size_t i = 0; i < tableDef.columns.size(); i++) {
        const ColumnDefinition& col = tableDef.columns[i];
        const std::string& val = (i < record.size()) ? record[i] : "";

        switch (col.type) {
            case ColumnType::INTEGER: {
                int32_t v = val.empty() ? 0 : std::stoi(val);
                std::memcpy(&buffer[pos], &v, 4);
                pos += 4;
                break;
            }
            case ColumnType::DOUBLE: {
                double v = val.empty() ? 0.0 : std::stod(val);
                std::memcpy(&buffer[pos], &v, 8);
                pos += 8;
                break;
            }
            case ColumnType::VARCHAR: {
                // Copiar hasta varcharSize bytes; si el string es mas corto el resto queda en cero
                size_t copyLen = std::min(val.size(), static_cast<size_t>(col.varcharSize));
                std::memcpy(&buffer[pos], val.c_str(), copyLen);
                pos += col.varcharSize;
                break;
            }
            case ColumnType::DATETIME: {
                int64_t v = parseDateTime(val);
                std::memcpy(&buffer[pos], &v, 8);
                pos += 8;
                break;
            }
        }
    }

    return buffer;
}

Row DataManager::deserialize(const TableDefinition& tableDef, const std::vector<char>& buffer) const {
    Row row;
    size_t pos = 1; // Saltar el deleted flag

    for (const auto& col : tableDef.columns) {
        switch (col.type) {
            case ColumnType::INTEGER: {
                int32_t v = 0;
                std::memcpy(&v, &buffer[pos], 4);
                row.push_back(std::to_string(v));
                pos += 4;
                break;
            }
            case ColumnType::DOUBLE: {
                double v = 0.0;
                std::memcpy(&v, &buffer[pos], 8);
                row.push_back(std::to_string(v));
                pos += 8;
                break;
            }
            case ColumnType::VARCHAR: {
                // Construir el string y cortar en el primer caracter nulo
                std::string s(buffer.begin() + pos, buffer.begin() + pos + col.varcharSize);
                s = s.c_str(); // std::string::c_str() corta en el primer '\0'
                row.push_back(s);
                pos += col.varcharSize;
                break;
            }
            case ColumnType::DATETIME: {
                int64_t v = 0;
                std::memcpy(&v, &buffer[pos], 8);
                row.push_back(formatDateTime(v));
                pos += 8;
                break;
            }
        }
    }

    return row;
}

// --------------------------------------------------------------------------
// Operaciones publicas
// --------------------------------------------------------------------------

bool DataManager::createTableFile(const TableDefinition& tableDef) {
    std::string path = tablePath(tableDef.dbName, tableDef.tableName);
    if (fs::exists(path)) return false;

    // Crear la carpeta de la base de datos si todavia no existe
    fs::create_directories(dataRoot + "/" + tableDef.dbName);

    // Crear el archivo y escribir un header de 0 registros
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;

    uint32_t count = 0;
    file.write(reinterpret_cast<char*>(&count), sizeof(count));
    return true;
}

bool DataManager::deleteTableFile(const std::string& dbName, const std::string& tableName) {
    std::string path = tablePath(dbName, tableName);
    if (!fs::exists(path)) return false;
    fs::remove(path);
    return true;
}

int64_t DataManager::writeRecord(const TableDefinition& tableDef, const Row& record) {
    std::string path = tablePath(tableDef.dbName, tableDef.tableName);

    // Leer cuantos registros hay actualmente para saber donde escribir el nuevo
    uint32_t count = readTotalCount(path);
    int64_t  offset = recordOffset(tableDef, count);

    // Serializar y cifrar
    std::vector<char> buffer = serialize(tableDef, record);
    xorCipher(buffer);

    // Abrir en modo append binario para no pisar el contenido existente
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file) throw std::runtime_error("No se pudo abrir el archivo de tabla: " + path);

    file.seekp(offset, std::ios::beg);
    file.write(buffer.data(), buffer.size());

    // Actualizar el conteo en el header
    writeTotalCount(path, count + 1);

    return offset;
}

Row DataManager::readRecord(const TableDefinition& tableDef, int64_t offset) {
    std::string path = tablePath(tableDef.dbName, tableDef.tableName);
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("No se pudo abrir el archivo de tabla: " + path);

    file.seekg(offset, std::ios::beg);

    std::vector<char> buffer(tableDef.recordSize());
    file.read(buffer.data(), buffer.size());

    // Descifrar antes de interpretar los bytes
    xorCipher(buffer);

    // Si el registro fue eliminado logicamente, retornar fila vacia
    if (static_cast<unsigned char>(buffer[0]) == 0xFF) return {};

    return deserialize(tableDef, buffer);
}

bool DataManager::deleteRecord(const TableDefinition& tableDef, int64_t offset) {
    std::string path = tablePath(tableDef.dbName, tableDef.tableName);
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file) return false;

    // Leer el registro, descifrar, cambiar el flag y volver a cifrar
    file.seekg(offset, std::ios::beg);
    std::vector<char> buffer(tableDef.recordSize());
    file.read(buffer.data(), buffer.size());

    xorCipher(buffer);
    buffer[0] = FLAG_DELETED;  // Marcar como eliminado
    xorCipher(buffer);         // Volver a cifrar con el nuevo flag

    // Sobreescribir en la misma posicion
    file.seekp(offset, std::ios::beg);
    file.write(buffer.data(), buffer.size());

    return true;
}

bool DataManager::updateRecord(const TableDefinition& tableDef, int64_t offset, const Row& newRecord) {
    std::string path = tablePath(tableDef.dbName, tableDef.tableName);
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file) return false;

    std::vector<char> buffer = serialize(tableDef, newRecord);
    xorCipher(buffer);

    file.seekp(offset, std::ios::beg);
    file.write(buffer.data(), buffer.size());

    return true;
}

std::vector<std::pair<int64_t, Row>> DataManager::scanAll(const TableDefinition& tableDef) {
    std::vector<std::pair<int64_t, Row>> results;
    std::string path = tablePath(tableDef.dbName, tableDef.tableName);

    std::ifstream file(path, std::ios::binary);
    if (!file) return results;

    // Leer el total de registros (incluidos eliminados) del header
    uint32_t totalCount = 0;
    file.read(reinterpret_cast<char*>(&totalCount), sizeof(totalCount));

    std::vector<char> buffer(tableDef.recordSize());

    for (uint32_t i = 0; i < totalCount; i++) {
        int64_t offset = recordOffset(tableDef, i);
        file.seekg(offset, std::ios::beg);
        file.read(buffer.data(), buffer.size());

        // Descifrar para verificar el deleted flag
        std::vector<char> decrypted = buffer;
        xorCipher(decrypted);

        // Solo agregar registros activos al resultado
        if (static_cast<unsigned char>(decrypted[0]) != 0xFF) {
            results.push_back({ offset, deserialize(tableDef, decrypted) });
        }
    }

    return results;
}

bool DataManager::isEmpty(const TableDefinition& tableDef) {
    return scanAll(tableDef).empty();
}