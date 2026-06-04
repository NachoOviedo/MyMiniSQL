//
// Created by nacho on 6/2/2026.
//

#ifndef MYMINISQL_DATAMANAGER_H
#define MYMINISQL_DATAMANAGER_H

#include <string>
#include <vector>
#include <utility>
#include "Types.h"

// DataManager gestiona la lectura y escritura de los archivos binarios de tablas.
//
// Formato de un archivo de tabla:
//   [HEADER: 4 bytes = uint32_t con el total de registros, incluidos los eliminados]
//   [REGISTRO 0: deleted_flag(1) + columnas en orden]
//   [REGISTRO 1: deleted_flag(1) + columnas en orden]
//   ...
//
// El deleted_flag vale 0x00 si el registro esta activo, 0xFF si fue eliminado logicamente.
// Todos los registros son de tamaño fijo, por lo que el offset de cualquier registro N
// se calcula en O(1): HEADER_SIZE + N * recordSize().
//
// Todos los datos se cifran con XOR antes de escribirse al disco.

class DataManager {
public:
    // dataRootPath: ruta a la carpeta "data/" donde viven las subcarpetas de cada base de datos
    explicit DataManager(const std::string& dataRootPath);

    // Crea la carpeta de la base de datos y el archivo .bin de la tabla.
    // Escribe la cabecera con 0 registros.
    // Retorna false si el archivo ya existe.
    bool createTableFile(const TableDefinition& tableDef);

    // Elimina el archivo .bin de la tabla del disco.
    bool deleteTableFile(const std::string& dbName, const std::string& tableName);

    // Inserta un nuevo registro al final del archivo.
    // Retorna el offset en bytes donde quedo guardado (para que los indices lo almacenen).
    int64_t writeRecord(const TableDefinition& tableDef, const Row& record);

    // Lee el registro ubicado en el offset indicado.
    // Retorna un Row vacio si el registro esta marcado como eliminado.
    Row readRecord(const TableDefinition& tableDef, int64_t offset);

    // Marca el registro en el offset dado como eliminado sin borrar el bloque del archivo.
    bool deleteRecord(const TableDefinition& tableDef, int64_t offset);

    // Sobreescribe el registro en el offset dado con nuevos valores.
    bool updateRecord(const TableDefinition& tableDef, int64_t offset, const Row& newRecord);

    // Recorre todo el archivo y retorna todos los registros activos.
    // Cada elemento es un par {offset, Row} para que los indices puedan usarlo.
    std::vector<std::pair<int64_t, Row>> scanAll(const TableDefinition& tableDef);

    // Retorna true si la tabla no tiene ningun registro activo (util para DROP TABLE).
    bool isEmpty(const TableDefinition& tableDef);

    // Retorna la ruta completa al archivo .bin de una tabla
    std::string tablePath(const std::string& dbName, const std::string& tableName) const;

private:
    std::string dataRoot; // Ruta raiz, ej: "../data"

    static const int64_t HEADER_SIZE  = 4;      // Bytes del header (uint32_t)
    static const char    FLAG_ACTIVE  = 0x00;   // Registro activo
    static const char    FLAG_DELETED = -1;     // 0xFF como char con signo

    // Calcula el offset en bytes del registro N dentro del archivo
    int64_t recordOffset(const TableDefinition& tableDef, uint32_t index) const;

    // Lee el conteo total de registros desde la cabecera del archivo
    uint32_t readTotalCount(const std::string& path) const;

    // Escribe el conteo total de registros en la cabecera del archivo
    void writeTotalCount(const std::string& path, uint32_t count) const;

    // Convierte un Row a un buffer de bytes listo para escribir al disco (sin cifrar)
    std::vector<char> serialize(const TableDefinition& tableDef, const Row& record) const;

    // Convierte un buffer de bytes leido del disco a un Row (ya descifrado)
    Row deserialize(const TableDefinition& tableDef, const std::vector<char>& buffer) const;

    // Aplica (o revierte) el cifrado XOR sobre el buffer. Es su propio inverso.
    void xorCipher(std::vector<char>& data) const;

    // Convierte el string "YYYY-MM-DD HH:MM:SS" a Unix timestamp
    int64_t parseDateTime(const std::string& dt) const;

    // Convierte un Unix timestamp al string "YYYY-MM-DD HH:MM:SS"
    std::string formatDateTime(int64_t timestamp) const;
};


#endif //MYMINISQL_DATAMANAGER_H