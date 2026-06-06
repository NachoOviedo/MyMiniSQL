//
// Created by nacho on 6/2/2026.
//

#ifndef MYMINISQL_PROCESADORDECONSULTAS_H
#define MYMINISQL_PROCESADORDECONSULTAS_H

#include <string>
#include "Types.h"
#include "SistemaDeCalogo.h"
#include "DataManager.h"

// ProcesadorDeConsultas es el cerebro del servidor.
// Recibe un string SQL puro desde el WebAPI, lo parsea, valida y ejecuta.
//
// Responsabilidades:
//   1. Parsear: identificar el tipo de sentencia y extraer sus parametros
//   2. Validar: verificar con el SistemaDeCalogo que todo sea semanticamente correcto
//   3. Ejecutar: coordinar las llamadas al DataManager para leer/escribir datos
//
// Lo que NO hace:
//   - Manejar JSON (eso es trabajo del WebAPI)
//   - Leer o escribir archivos directamente (eso es trabajo del DataManager)

class ProcesadorDeConsultas {
public:
    // El procesador necesita acceso al catalogo y al gestor de datos
    ProcesadorDeConsultas(SistemaDeCalogo& catalogo, DataManager& dataManager);

    // Punto de entrada principal. Recibe el SQL y retorna el resultado.
    // El dbContexto es la base de datos activa en el cliente (puede estar vacia).
    QueryResult ejecutar(const std::string& sql, const std::string& dbContexto);

private:
    SistemaDeCalogo& catalogo;
    DataManager&     dataManager;

    // ---------- Handlers por tipo de sentencia ----------
    // Cada uno recibe el SQL completo (en mayusculas y sin espacios extra)
    // y la base de datos activa en el cliente.

    QueryResult handleCreateDatabase(const std::string& sql);
    QueryResult handleSetDatabase(const std::string& sql);
    QueryResult handleCreateTable(const std::string& sql, const std::string& dbContexto);
    QueryResult handleDropTable(const std::string& sql, const std::string& dbContexto);
    QueryResult handleInsert(const std::string& sql, const std::string& dbContexto);
    QueryResult handleSelect(const std::string& sql, const std::string& dbContexto);
    QueryResult handleUpdate(const std::string& sql, const std::string& dbContexto);
    QueryResult handleDelete(const std::string& sql, const std::string& dbContexto);
    QueryResult handleCreateIndex(const std::string& sql, const std::string& dbContexto);

    // ---------- Helpers de parsing ----------

    // Convierte el string a mayusculas
    std::string toUpper(const std::string& s) const;

    // Elimina espacios al inicio y al final del string
    std::string trim(const std::string& s) const;

    // Divide el string por un delimitador y retorna un vector de tokens
    std::vector<std::string> split(const std::string& s, char delim) const;

    // Extrae el contenido entre el primer '(' y el ultimo ')' del string
    std::string extractParenContent(const std::string& s) const;

    // Parsea la lista de columnas de un CREATE TABLE y retorna las definiciones
    // Ejemplo de entrada: "ID INTEGER, Nombre VARCHAR(30), FechaNac DATETIME"
    std::vector<ColumnDefinition> parseColumnDefinitions(const std::string& colStr) const;

    // Parsea la lista de valores de un INSERT INTO y retorna un Row
    // Ejemplo de entrada: "1, \"Isaac\", \"Ramirez\", \"2000-01-01 01:02:00\""
    Row parseInsertValues(const std::string& valStr) const;

    // Retorna un QueryResult de error con el mensaje dado
    QueryResult error(const std::string& mensaje) const;

    // Retorna un QueryResult de exito con el mensaje dado
    QueryResult ok(const std::string& mensaje) const;
};

#endif //MYMINISQL_PROCESADORDECONSULTAS_H