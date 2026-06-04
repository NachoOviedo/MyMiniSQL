//
// Created by nacho on 6/4/2026.
//

#ifndef MYMINISQL_TYPES_H
#define MYMINISQL_TYPES_H

#include <string>
#include <vector>
#include <cstdint>

// Tipos de columna que soporta TinySQLDb
// Cada tipo define cuantos bytes ocupa en el archivo binario
enum class ColumnType {
    INTEGER,    // 4 bytes  - int32_t
    DOUBLE,     // 8 bytes  - double
    VARCHAR,    // N bytes  - char fijo de longitud N (definida al crear la tabla)
    DATETIME    // 8 bytes  - int64_t (Unix timestamp en segundos)
};

// Definicion de una sola columna dentro de una tabla
struct ColumnDefinition {
    std::string name;       // Nombre de la columna, ej: "Nombre"
    ColumnType  type;       // Tipo de dato
    uint32_t    varcharSize;// Solo se usa cuando type == VARCHAR, indica el N de VARCHAR(N)
    bool        nullable;   // true si admite NULL
    uint32_t    colIndex;   // Posicion de la columna en la tabla (0-based), para ordenar al leer el catalogo

    // Retorna cuantos bytes ocupa esta columna en un registro binario
    uint32_t byteSize() const {
        switch (type) {
            case ColumnType::INTEGER:  return 4;
            case ColumnType::DOUBLE:   return 8;
            case ColumnType::DATETIME: return 8;
            case ColumnType::VARCHAR:  return varcharSize;
            default: return 0;
        }
    }
};

// Definicion completa de una tabla: su nombre, base de datos y lista de columnas
struct TableDefinition {
    std::string dbName;
    std::string tableName;
    std::vector<ColumnDefinition> columns; // En orden de creacion

    // Calcula el tamaño en bytes de un registro completo.
    // Estructura: [1 byte deleted_flag][col0][col1]...[colN]
    uint32_t recordSize() const {
        uint32_t size = 1; // El deleted flag siempre ocupa el primer byte
        for (const auto& col : columns) {
            size += col.byteSize();
        }
        return size;
    }
};

// Definicion de un indice creado sobre una columna
struct IndexDefinition {
    std::string indexName;  // Nombre del indice, ej: "Estudiante_Id"
    std::string tableName;  // Tabla sobre la que aplica
    std::string dbName;     // Base de datos a la que pertenece la tabla
    std::string colName;    // Columna indexada
    std::string type;       // "BTREE" o "BST"
};

// Alias para mayor legibilidad en el resto del codigo
using Row    = std::vector<std::string>;    // Una fila de datos (todo como string para uniformidad)
using Matrix = std::vector<Row>;            // Conjunto de filas (resultado de un SELECT)

// Resultado que el QueryProcessor devuelve al WebAPI despues de ejecutar cualquier sentencia
struct QueryResult {
    bool        success;                    // true si la operacion termino sin errores
    std::string message;                    // Mensaje de confirmacion o de error
    std::vector<std::string> columns;       // Nombres de columnas (util para SELECT)
    Matrix      rows;                       // Filas de datos (util para SELECT)
    double      time_ms = 0.0;             // Tiempo de ejecucion medido por el WebAPI
};

#endif //MYMINISQL_TYPES_H