//
// Created by nacho on 6/2/2026.
//

#ifndef MYMINISQL_SISTEMADECALOGO_H
#define MYMINISQL_SISTEMADECALOGO_H

#include <string>
#include <vector>
#include <unordered_map>
#include "Types.h"

// SistemaDeCalogo gestiona los metadatos de todas las bases de datos.
//
// Persiste en 4 archivos binarios de registros de tamaño fijo dentro de "data/system_catalog/":
//   SystemDatabases : lista de bases de datos existentes
//   SystemTables    : lista de tablas y a que base de datos pertenecen
//   SystemColumns   : columnas de cada tabla con su tipo y configuracion
//   SystemIndexes   : indices creados sobre columnas de tablas
//
// Al iniciar el servidor, el catalogo lee todos los archivos y carga la informacion
// en memoria (maps), de modo que las consultas al catalogo no requieren acceso a disco.
// Solo se accede al disco cuando se crea o elimina algo.

class SistemaDeCalogo {
public:
    // catalogPath: ruta a la carpeta "data/system_catalog/"
    explicit SistemaDeCalogo(const std::string& catalogPath);

    // Carga en memoria todos los datos de los 4 archivos binarios.
    // Debe llamarse una sola vez al arrancar el servidor.
    void load();

    // ---------- Bases de datos ----------

    bool createDatabase(const std::string& name);
    bool databaseExists(const std::string& name) const;
    std::vector<std::string> listDatabases() const;

    // ---------- Tablas ----------

    // Registra la definicion de la tabla en el catalogo y crea su archivo binario.
    bool createTable(const TableDefinition& tableDef);

    bool tableExists(const std::string& dbName, const std::string& tableName) const;

    // Retorna la definicion completa de una tabla (columnas incluidas).
    TableDefinition getTableDef(const std::string& dbName, const std::string& tableName) const;

    // Elimina la tabla del catalogo. El DataManager se encarga de borrar el archivo .bin.
    bool dropTable(const std::string& dbName, const std::string& tableName);

    std::vector<std::string> listTables(const std::string& dbName) const;

    // ---------- Indices ----------

    bool addIndex(const IndexDefinition& idx);
    bool indexExists(const std::string& tableName, const std::string& colName) const;
    IndexDefinition getIndex(const std::string& tableName, const std::string& colName) const;
    std::vector<IndexDefinition> listIndexes() const;
    bool dropIndex(const std::string& indexName);

private:
    std::string catalogPath;

    // Representacion en memoria para acceso rapido sin leer disco
    std::vector<std::string>                         databases;  // Nombres de bases de datos activas
    std::unordered_map<std::string, TableDefinition> tables;     // Clave: "dbName.tableName"
    std::vector<IndexDefinition>                     indexes;

    // Construye la clave usada en el map de tablas
    std::string tableKey(const std::string& dbName, const std::string& tableName) const;

    // Rutas a cada archivo del catalogo
    std::string dbsFilePath()     const;
    std::string tablesFilePath()  const;
    std::string columnsFilePath() const;
    std::string indexesFilePath() const;

    // ---------- Estructuras binarias fijas para cada archivo ----------

    // Registro en SystemDatabases (65 bytes)
    struct DbRecord {
        char    name[64];   // Nombre de la base de datos
        uint8_t active;     // 1 = existe, 0 = eliminada
    };

    // Registro en SystemTables (129 bytes)
    struct TableRecord {
        char    tableName[64];
        char    dbName[64];
        uint8_t active;
    };

    // Registro en SystemColumns (203 bytes)
    struct ColumnRecord {
        char     tableName[64];
        char     dbName[64];
        char     colName[64];
        uint8_t  type;          // 0=INTEGER, 1=DOUBLE, 2=VARCHAR, 3=DATETIME
        uint32_t varcharSize;   // Solo relevante si type == 2
        uint8_t  nullable;
        uint32_t colIndex;      // Posicion de la columna en la tabla (para reconstruir el orden)
        uint8_t  active;
    };

    // Registro en SystemIndexes (265 bytes)
    struct IndexRecord {
        char    indexName[64];
        char    tableName[64];
        char    dbName[64];
        char    colName[64];
        char    type[8];        // "BTREE" o "BST"
        uint8_t active;
    };

    // Escribe un nuevo registro al final de un archivo binario del catalogo
    template<typename T>
    void appendRecord(const std::string& filePath, const T& record) const;

    // Lee todos los registros activos de un archivo binario del catalogo
    template<typename T>
    std::vector<T> readAllRecords(const std::string& filePath) const;

    // Marca como inactivo el primer registro que cumpla con el predicado dado
    template<typename T, typename Predicate>
    bool deactivateRecord(const std::string& filePath, Predicate pred) const;

    // Convierte el enum ColumnType a su representacion como uint8_t para guardar en disco
    static uint8_t typeToUint(ColumnType t);

    // Convierte el uint8_t leido del disco al enum ColumnType correspondiente
    static ColumnType uintToType(uint8_t v);
};


#endif //MYMINISQL_SISTEMADECALOGO_H