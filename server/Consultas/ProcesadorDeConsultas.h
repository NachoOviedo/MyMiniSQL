//
// ProcesadorDeConsultas.h
// Motor de base de datos TinySQLDb
//

#ifndef MYMINISQL_PROCESADORDECONSULTAS_H
#define MYMINISQL_PROCESADORDECONSULTAS_H

#include <string>
#include <vector>
#include "Types.h"
#include "SistemaDeCalogo.h"
#include "DataManager.h"

// ProcesadorDeConsultas es el cerebro del servidor.
// Recibe un string SQL puro desde el WebAPI, lo parsea, valida y ejecuta.
//
// Responsabilidades:
//   1. Parsear  : identificar el tipo de sentencia y extraer sus parámetros
//   2. Validar  : verificar con SistemaDeCalogo que todo sea semánticamente correcto
//   3. Ejecutar : coordinar las llamadas a DataManager para leer/escribir datos
//
// Lo que NO hace:
//   - Manejar JSON (trabajo del WebAPI)
//   - Leer o escribir archivos directamente (trabajo del DataManager)

class ProcesadorDeConsultas {
public:
    ProcesadorDeConsultas(SistemaDeCalogo& catalogo, DataManager& dataManager);

    // Punto de entrada principal.
    // sql        : sentencia SQL sin el punto y coma final (o con él, se ignora)
    // dbContexto : base de datos activa en el cliente (puede estar vacía)
    QueryResult ejecutar(const std::string& sql, const std::string& dbContexto);

private:
    SistemaDeCalogo& catalogo;
    DataManager&     dataManager;

    // ----------------------------------------------------------------
    // Handlers por tipo de sentencia
    // ----------------------------------------------------------------
    QueryResult handleCreateDatabase(const std::string& sql);
    QueryResult handleSetDatabase   (const std::string& sql);
    QueryResult handleCreateTable   (const std::string& sql, const std::string& dbContexto);
    QueryResult handleDropTable     (const std::string& sql, const std::string& dbContexto);
    QueryResult handleInsert        (const std::string& sql, const std::string& dbContexto);
    QueryResult handleSelect        (const std::string& sql, const std::string& dbContexto);
    QueryResult handleUpdate        (const std::string& sql, const std::string& dbContexto);
    QueryResult handleDelete        (const std::string& sql, const std::string& dbContexto);
    QueryResult handleCreateIndex   (const std::string& sql, const std::string& dbContexto);

    // ----------------------------------------------------------------
    // Estructura para una condición WHERE descompuesta
    // ----------------------------------------------------------------
    struct WhereCondition {
        std::string colName;    // Columna a comparar
        std::string op;         // Operador: =, >, <, >=, <=, <>, !=, LIKE, NOT
        std::string value;      // Valor (sin comillas)
        ColumnType  colType = ColumnType::VARCHAR; // Tipo de la columna (para comparación)
    };

    // Parsea "col op valor" en un WhereCondition.
    // Lanza std::runtime_error si la sintaxis es inválida o la columna no existe.
    WhereCondition parseWhereCondition(const std::string& whereStr,
                                       const TableDefinition& def) const;

    // Evalúa si una fila cumple una condición WHERE.
    bool evaluateCondition(const Row& row,
                           const WhereCondition& cond,
                           const TableDefinition& def) const;

    // ----------------------------------------------------------------
    // Quicksort para ORDER BY
    // data     : vector de pares {filaCompleta, filaProyectada}
    // colIdx   : índice de columna a comparar dentro del par
    // useFirst : true  → comparar en data[i].first  (fila completa)
    //            false → comparar en data[i].second (fila proyectada)
    // ----------------------------------------------------------------
    void quicksort(std::vector<std::pair<Row, Row>>& data,
                   int lo, int hi,
                   int colIdx, ColumnType colType,
                   bool descending, bool useFirst) const;

    int  partition(std::vector<std::pair<Row, Row>>& data,
                   int lo, int hi,
                   int colIdx, ColumnType colType,
                   bool descending, bool useFirst) const;

    // Compara dos valores string según el ColumnType (numérico o lexicográfico).
    // Retorna true si a < b.
    bool compareValues(const std::string& a,
                       const std::string& b,
                       ColumnType colType) const;

    // ----------------------------------------------------------------
    // Helpers de parsing
    // ----------------------------------------------------------------

    // Convierte el string a mayúsculas
    std::string toUpper(const std::string& s) const;

    // Elimina espacios al inicio y al final
    std::string trim(const std::string& s) const;

    // Divide s por delim respetando comillas dobles y paréntesis anidados
    std::vector<std::string> split(const std::string& s, char delim) const;

    // Extrae el contenido entre el primer '(' y el último ')' del string
    std::string extractParenContent(const std::string& s) const;

    // Parsea la lista de columnas de un CREATE TABLE
    // Ejemplo: "ID INTEGER, Nombre VARCHAR(30), FechaNac DATETIME"
    std::vector<ColumnDefinition> parseColumnDefinitions(const std::string& colStr) const;

    // Parsea la lista de valores de un INSERT INTO (sin las comillas externas)
    // Ejemplo: "1, \"Isaac\", \"Ramirez\", \"2000-01-01 01:02:00\""
    Row parseInsertValues(const std::string& valStr) const;

    // ----------------------------------------------------------------
    // Helpers de resultado
    // ----------------------------------------------------------------
    QueryResult error(const std::string& mensaje) const;
    QueryResult ok   (const std::string& mensaje) const;
};

#endif // MYMINISQL_PROCESADORDECONSULTAS_H