//
// Created by nacho on 6/2/2026.
//

#include "ProcesadorDeConsultas.h"
#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <cctype>

// --------------------------------------------------------------------------
// Constructor
// --------------------------------------------------------------------------

ProcesadorDeConsultas::ProcesadorDeConsultas(SistemaDeCalogo& catalogo, DataManager& dataManager)
    : catalogo(catalogo), dataManager(dataManager) {}

// --------------------------------------------------------------------------
// Punto de entrada
// --------------------------------------------------------------------------

QueryResult ProcesadorDeConsultas::ejecutar(const std::string& sql, const std::string& dbContexto) {
    // Normalizar: quitar espacios extremos y convertir a mayusculas para comparar keywords.
    // El string normalizado solo se usa para detectar el tipo de sentencia.
    // Los valores originales (nombres de tablas, datos) los extraemos del sql original.
    std::string sqlTrim  = trim(sql);
    std::string sqlUpper = toUpper(sqlTrim);

    if (sqlUpper.empty()) return error("Sentencia vacia.");

    // Identificar el tipo de sentencia por la primera palabra clave
    if (sqlUpper.rfind("CREATE DATABASE", 0) == 0) return handleCreateDatabase(sqlTrim);
    if (sqlUpper.rfind("SET DATABASE",    0) == 0) return handleSetDatabase(sqlTrim);
    if (sqlUpper.rfind("CREATE TABLE",    0) == 0) return handleCreateTable(sqlTrim, dbContexto);
    if (sqlUpper.rfind("DROP TABLE",      0) == 0) return handleDropTable(sqlTrim, dbContexto);
    if (sqlUpper.rfind("INSERT INTO",     0) == 0) return handleInsert(sqlTrim, dbContexto);
    if (sqlUpper.rfind("SELECT",          0) == 0) return handleSelect(sqlTrim, dbContexto);
    if (sqlUpper.rfind("UPDATE",          0) == 0) return handleUpdate(sqlTrim, dbContexto);
    if (sqlUpper.rfind("DELETE",          0) == 0) return handleDelete(sqlTrim, dbContexto);
    if (sqlUpper.rfind("CREATE INDEX",    0) == 0) return handleCreateIndex(sqlTrim, dbContexto);

    return error("Sentencia no reconocida: " + sqlTrim);
}

// --------------------------------------------------------------------------
// CREATE DATABASE <nombre>
// --------------------------------------------------------------------------

QueryResult ProcesadorDeConsultas::handleCreateDatabase(const std::string& sql) {
    // Formato esperado: "CREATE DATABASE NombreDB"
    // Extraemos todo lo que viene despues de "CREATE DATABASE "
    std::string prefix = "CREATE DATABASE ";
    if (sql.size() <= prefix.size()) return error("Falta el nombre de la base de datos.");

    std::string nombre = trim(sql.substr(prefix.size()));
    if (nombre.empty()) return error("El nombre de la base de datos no puede estar vacio.");

    if (catalogo.databaseExists(nombre)) {
        return error("La base de datos '" + nombre + "' ya existe.");
    }

    catalogo.createDatabase(nombre);

    // Crear la carpeta fisica de la base de datos a traves del DataManager
    // (createTableFile ya crea la carpeta, pero para la BD en si usamos filesystem directamente)
    // Esto lo maneja el SistemaDeCalogo internamente al crear el registro.

    return ok("Base de datos '" + nombre + "' creada exitosamente.");
}

// --------------------------------------------------------------------------
// SET DATABASE <nombre>
// --------------------------------------------------------------------------

QueryResult ProcesadorDeConsultas::handleSetDatabase(const std::string& sql) {
    // Formato esperado: "SET DATABASE NombreDB"
    // El servidor solo valida que exista. El cliente guarda el contexto.
    std::string prefix = "SET DATABASE ";
    if (sql.size() <= prefix.size()) return error("Falta el nombre de la base de datos.");

    std::string nombre = trim(sql.substr(prefix.size()));
    if (!catalogo.databaseExists(nombre)) {
        return error("La base de datos '" + nombre + "' no existe.");
    }

    // Retornamos el nombre en el mensaje para que el cliente lo tome como contexto
    QueryResult result = ok("Contexto establecido en '" + nombre + "'.");
    result.message = nombre; // El WebAPI va a leer esto para pasarlo al cliente
    return result;
}

// --------------------------------------------------------------------------
// CREATE TABLE <nombre> AS ( <columnas> )
// --------------------------------------------------------------------------

QueryResult ProcesadorDeConsultas::handleCreateTable(const std::string& sql, const std::string& dbContexto) {
    if (dbContexto.empty()) return error("No hay base de datos seleccionada. Usa SET DATABASE primero.");

    // Extraer el nombre de la tabla: lo que hay entre "CREATE TABLE " y el primer "("
    std::string prefix = "CREATE TABLE ";
    std::string resto  = trim(sql.substr(prefix.size()));

    size_t parenPos = resto.find('(');
    if (parenPos == std::string::npos) return error("Falta la definicion de columnas entre parentesis.");

    std::string tableName = trim(resto.substr(0, parenPos));
    if (tableName.empty()) return error("El nombre de la tabla no puede estar vacio.");

    // Quitar el "AS" si el usuario lo escribio (es opcional segun el enunciado)
    if (toUpper(tableName).rfind(" AS", tableName.size() - 3) != std::string::npos) {
        tableName = trim(tableName.substr(0, tableName.size() - 3));
    }

    if (catalogo.tableExists(dbContexto, tableName)) {
        return error("La tabla '" + tableName + "' ya existe en '" + dbContexto + "'.");
    }

    // Extraer el contenido entre parentesis y parsear las columnas
    std::string colContent = extractParenContent(resto);
    if (colContent.empty()) return error("La definicion de columnas esta vacia.");

    std::vector<ColumnDefinition> columnas;
    try {
        columnas = parseColumnDefinitions(colContent);
    } catch (const std::exception& e) {
        return error(std::string("Error al parsear columnas: ") + e.what());
    }

    if (columnas.empty()) return error("La tabla debe tener al menos una columna.");

    // Armar la definicion completa de la tabla
    TableDefinition tableDef;
    tableDef.dbName    = dbContexto;
    tableDef.tableName = tableName;
    tableDef.columns   = columnas;

    // Registrar en el catalogo y crear el archivo binario
    catalogo.createTable(tableDef);
    dataManager.createTableFile(tableDef);

    return ok("Tabla '" + tableName + "' creada exitosamente en '" + dbContexto + "'.");
}

// --------------------------------------------------------------------------
// DROP TABLE <nombre>
// --------------------------------------------------------------------------

QueryResult ProcesadorDeConsultas::handleDropTable(const std::string& sql, const std::string& dbContexto) {
    if (dbContexto.empty()) return error("No hay base de datos seleccionada.");

    std::string prefix = "DROP TABLE ";
    std::string tableName = trim(sql.substr(prefix.size()));

    if (!catalogo.tableExists(dbContexto, tableName)) {
        return error("La tabla '" + tableName + "' no existe.");
    }

    // El enunciado exige que la tabla este vacia antes de eliminarla
    TableDefinition tableDef = catalogo.getTableDef(dbContexto, tableName);
    if (!dataManager.isEmpty(tableDef)) {
        return error("No se puede eliminar la tabla '" + tableName + "' porque contiene datos.");
    }

    catalogo.dropTable(dbContexto, tableName);
    dataManager.deleteTableFile(dbContexto, tableName);

    return ok("Tabla '" + tableName + "' eliminada.");
}

// --------------------------------------------------------------------------
// INSERT INTO <tabla> VALUES(<valores>)
// --------------------------------------------------------------------------

QueryResult ProcesadorDeConsultas::handleInsert(const std::string& sql, const std::string& dbContexto) {
    if (dbContexto.empty()) return error("No hay base de datos seleccionada.");

    // Formato: INSERT INTO NombreTabla VALUES(v1, v2, ...)
    std::string prefix = "INSERT INTO ";
    std::string resto  = trim(sql.substr(prefix.size()));

    // El nombre de la tabla es lo que hay antes de "VALUES"
    std::string upperResto = toUpper(resto);
    size_t valuesPos = upperResto.find("VALUES");
    if (valuesPos == std::string::npos) return error("Falta la palabra clave VALUES.");

    std::string tableName = trim(resto.substr(0, valuesPos));
    if (tableName.empty()) return error("Falta el nombre de la tabla.");

    if (!catalogo.tableExists(dbContexto, tableName)) {
        return error("La tabla '" + tableName + "' no existe.");
    }

    // Extraer los valores entre parentesis
    std::string afterValues = trim(resto.substr(valuesPos + 6)); // 6 = len("VALUES")
    std::string valContent  = extractParenContent(afterValues);

    TableDefinition tableDef = catalogo.getTableDef(dbContexto, tableName);

    Row valores;
    try {
        valores = parseInsertValues(valContent);
    } catch (const std::exception& e) {
        return error(std::string("Error al parsear valores: ") + e.what());
    }

    // Validar que la cantidad de valores coincide con la cantidad de columnas
    if (valores.size() != tableDef.columns.size()) {
        return error("La cantidad de valores (" + std::to_string(valores.size()) +
                     ") no coincide con la cantidad de columnas (" +
                     std::to_string(tableDef.columns.size()) + ").");
    }

    // Escribir el registro en el archivo binario
    try {
        dataManager.writeRecord(tableDef, valores);
    } catch (const std::exception& e) {
        return error(std::string("Error al escribir el registro: ") + e.what());
    }

    return ok("1 registro insertado en '" + tableName + "'.");
}

// --------------------------------------------------------------------------
// SELECT (implementacion basica - se expande en semana 2)
// --------------------------------------------------------------------------

QueryResult ProcesadorDeConsultas::handleSelect(const std::string& sql, const std::string& dbContexto) {
    if (dbContexto.empty()) return error("No hay base de datos seleccionada.");

    // Por ahora solo soportamos: SELECT * FROM <tabla>
    // La logica completa con WHERE, ORDER BY e indices se agrega en semana 2
    std::string upperSql = toUpper(sql);

    size_t fromPos = upperSql.find(" FROM ");
    if (fromPos == std::string::npos) return error("Falta la clausula FROM.");

    // Extraer que columnas se piden (entre SELECT y FROM)
    std::string colPart   = trim(sql.substr(7, fromPos - 7)); // 7 = len("SELECT ")
    std::string afterFrom = trim(sql.substr(fromPos + 6));    // 6 = len(" FROM ")

    // El nombre de la tabla es la primera palabra despues de FROM
    // (puede haber WHERE u ORDER BY despues, los ignoramos por ahora)
    std::string tableName = split(afterFrom, ' ')[0];
    tableName = trim(tableName);

    if (!catalogo.tableExists(dbContexto, tableName)) {
        return error("La tabla '" + tableName + "' no existe.");
    }

    TableDefinition tableDef = catalogo.getTableDef(dbContexto, tableName);
    auto registros = dataManager.scanAll(tableDef);

    QueryResult result;
    result.success = true;
    result.message = std::to_string(registros.size()) + " fila(s) retornadas.";

    // Armar los nombres de columnas para la respuesta
    for (const auto& col : tableDef.columns) {
        result.columns.push_back(col.name);
    }

    // Agregar las filas
    for (const auto& [offset, row] : registros) {
        result.rows.push_back(row);
    }

    return result;
}

// --------------------------------------------------------------------------
// UPDATE, DELETE, CREATE INDEX (stubs para semana 2)
// --------------------------------------------------------------------------

QueryResult ProcesadorDeConsultas::handleUpdate(const std::string& sql, const std::string& dbContexto) {
    return error("UPDATE se implementa en semana 2.");
}

QueryResult ProcesadorDeConsultas::handleDelete(const std::string& sql, const std::string& dbContexto) {
    return error("DELETE se implementa en semana 2.");
}

QueryResult ProcesadorDeConsultas::handleCreateIndex(const std::string& sql, const std::string& dbContexto) {
    return error("CREATE INDEX se implementa en semana 2.");
}

// --------------------------------------------------------------------------
// Helpers de parsing
// --------------------------------------------------------------------------

std::string ProcesadorDeConsultas::toUpper(const std::string& s) const {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), ::toupper);
    return result;
}

std::string ProcesadorDeConsultas::trim(const std::string& s) const {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::vector<std::string> ProcesadorDeConsultas::split(const std::string& s, char delim) const {
    std::vector<std::string> tokens;
    std::istringstream stream(s);
    std::string token;
    while (std::getline(stream, token, delim)) {
        tokens.push_back(token);
    }
    return tokens;
}

std::string ProcesadorDeConsultas::extractParenContent(const std::string& s) const {
    size_t open  = s.find('(');
    size_t close = s.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close <= open) return "";
    return trim(s.substr(open + 1, close - open - 1));
}

std::vector<ColumnDefinition> ProcesadorDeConsultas::parseColumnDefinitions(const std::string& colStr) const {
    // Entrada esperada: "ID INTEGER, Nombre VARCHAR(30), FechaNac DATETIME"
    // Cada columna se separa por coma, pero hay que tener cuidado con VARCHAR(N)
    // que tambien contiene comas dentro si hubiera multiples parametros (no es el caso aqui).

    std::vector<ColumnDefinition> columnas;

    // Separar por coma respetando los parentesis de VARCHAR(N)
    std::vector<std::string> parts;
    int depth = 0;
    std::string current;
    for (char c : colStr) {
        if (c == '(') depth++;
        else if (c == ')') depth--;

        if (c == ',' && depth == 0) {
            parts.push_back(trim(current));
            current.clear();
        } else {
            current += c;
        }
    }
    if (!trim(current).empty()) parts.push_back(trim(current));

    for (uint32_t i = 0; i < parts.size(); i++) {
        std::string part = parts[i];
        ColumnDefinition col;
        col.colIndex    = i;
        col.nullable    = true;  // Por defecto nullable
        col.varcharSize = 0;

        // El nombre de la columna es la primera palabra, el tipo es el resto
        size_t spacePos = part.find(' ');
        if (spacePos == std::string::npos) throw std::runtime_error("Columna mal formada: " + part);

        col.name          = trim(part.substr(0, spacePos));
        std::string typePart = trim(toUpper(part.substr(spacePos + 1)));

        if (typePart == "INTEGER") {
            col.type = ColumnType::INTEGER;
        } else if (typePart == "DOUBLE") {
            col.type = ColumnType::DOUBLE;
        } else if (typePart == "DATETIME") {
            col.type = ColumnType::DATETIME;
        } else if (typePart.rfind("VARCHAR", 0) == 0) {
            col.type = ColumnType::VARCHAR;
            // Extraer el N de VARCHAR(N)
            std::string nStr = extractParenContent(typePart);
            if (nStr.empty()) throw std::runtime_error("VARCHAR requiere tamaño: VARCHAR(N)");
            col.varcharSize = static_cast<uint32_t>(std::stoi(nStr));
        } else {
            throw std::runtime_error("Tipo de columna no reconocido: " + typePart);
        }

        columnas.push_back(col);
    }

    return columnas;
}

Row ProcesadorDeConsultas::parseInsertValues(const std::string& valStr) const {
    // Entrada esperada: 1, "Isaac", "Ramirez", "2000-01-01 01:02:00"
    // Los strings vienen entre comillas dobles, los numeros sin comillas.

    Row valores;
    bool inString = false;
    std::string current;

    for (size_t i = 0; i < valStr.size(); i++) {
        char c = valStr[i];

        if (c == '"') {
            inString = !inString; // Abrir o cerrar string
        } else if (c == ',' && !inString) {
            // Separador entre valores (solo fuera de strings)
            valores.push_back(trim(current));
            current.clear();
        } else {
            current += c;
        }
    }
    // Agregar el ultimo valor
    if (!trim(current).empty() || inString == false) {
        valores.push_back(trim(current));
    }

    return valores;
}

QueryResult ProcesadorDeConsultas::error(const std::string& mensaje) const {
    QueryResult result;
    result.success = false;
    result.message = mensaje;
    return result;
}

QueryResult ProcesadorDeConsultas::ok(const std::string& mensaje) const {
    QueryResult result;
    result.success = true;
    result.message = mensaje;
    return result;
}