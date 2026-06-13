//
// ProcesadorDeConsultas.cpp
// Motor de base de datos TinySQLDb
//

#include "ProcesadorDeConsultas.h"
#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <chrono>

// ============================================================
// Constructor
// ============================================================

ProcesadorDeConsultas::ProcesadorDeConsultas(SistemaDeCalogo& catalogo, DataManager& dataManager)
    : catalogo(catalogo), dataManager(dataManager) {}

// ============================================================
// Punto de entrada principal
// ============================================================

QueryResult ProcesadorDeConsultas::ejecutar(const std::string& sql, const std::string& dbContexto) {
    std::string s = trim(sql);
    if (s.empty()) return error("Sentencia vacía.");

    // Normalizar: sacar el verbo en mayúsculas para el dispatch
    std::string upper = toUpper(s);

    // Quitar punto y coma final si existe
    if (!upper.empty() && upper.back() == ';') {
        upper.pop_back();
        s.pop_back();
        upper = trim(upper);
        s     = trim(s);
    }

    // ---- Dispatch por primera(s) palabra(s) ----
    if (upper.rfind("CREATE DATABASE", 0) == 0)
        return handleCreateDatabase(s);

    if (upper.rfind("SET DATABASE", 0) == 0)
        return handleSetDatabase(s);

    if (upper.rfind("CREATE INDEX", 0) == 0)
        return handleCreateIndex(s, dbContexto);

    if (upper.rfind("CREATE TABLE", 0) == 0)
        return handleCreateTable(s, dbContexto);

    if (upper.rfind("DROP TABLE", 0) == 0)
        return handleDropTable(s, dbContexto);

    if (upper.rfind("INSERT INTO", 0) == 0)
        return handleInsert(s, dbContexto);

    if (upper.rfind("SELECT", 0) == 0)
        return handleSelect(s, dbContexto);

    if (upper.rfind("UPDATE", 0) == 0)
        return handleUpdate(s, dbContexto);

    if (upper.rfind("DELETE", 0) == 0)
        return handleDelete(s, dbContexto);

    return error("Sentencia SQL no reconocida: " + s);
}

// ============================================================
// CREATE DATABASE
// ============================================================

QueryResult ProcesadorDeConsultas::handleCreateDatabase(const std::string& sql) {
    // CREATE DATABASE <name>
    std::string upper = toUpper(sql);
    std::string rest  = trim(sql.substr(15)); // después de "CREATE DATABASE"
    if (rest.empty())
        return error("Falta el nombre de la base de datos.");

    std::string name = trim(rest);
    // Validar que sea un identificador simple
    for (char c : name) {
        if (!std::isalnum(c) && c != '_')
            return error("Nombre de base de datos inválido: " + name);
    }

    if (catalogo.databaseExists(name))
        return error("La base de datos '" + name + "' ya existe.");

    if (!catalogo.createDatabase(name))
        return error("No se pudo crear la base de datos '" + name + "'.");

    return ok("Base de datos '" + name + "' creada exitosamente.");
}

// ============================================================
// SET DATABASE
// ============================================================

QueryResult ProcesadorDeConsultas::handleSetDatabase(const std::string& sql) {
    // SET DATABASE <name>
    std::string rest = trim(sql.substr(12)); // después de "SET DATABASE"
    if (rest.empty())
        return error("Falta el nombre de la base de datos.");

    std::string name = trim(rest);
    if (!catalogo.databaseExists(name))
        return error("La base de datos '" + name + "' no existe.");

    return ok("Contexto establecido en '" + name + "'.");
}

// ============================================================
// CREATE TABLE
// ============================================================

QueryResult ProcesadorDeConsultas::handleCreateTable(const std::string& sql, const std::string& dbContexto) {
    if (dbContexto.empty())
        return error("No hay base de datos activa. Use SET DATABASE primero.");

    // CREATE TABLE <name> AS ( <cols> )   — con o sin AS
    // Acepta tanto "CREATE TABLE X AS (...)" como "CREATE TABLE X (...)"
    std::string upper = toUpper(sql);

    // Extraer nombre de tabla
    // Formato: CREATE TABLE nombre [AS] (...)
    std::string afterCreate = trim(sql.substr(12)); // después de "CREATE TABLE"
    if (afterCreate.empty()) return error("Sintaxis inválida en CREATE TABLE.");

    // Primer token = nombre de tabla
    std::istringstream iss(afterCreate);
    std::string tableName;
    iss >> tableName;

    // Buscar el paréntesis de apertura
    std::string remaining = afterCreate.substr(tableName.size());
    remaining = trim(remaining);

    // Saltar "AS" si existe
    std::string upperRem = toUpper(remaining);
    if (upperRem.rfind("AS", 0) == 0) {
        remaining = trim(remaining.substr(2));
    }

    // Extraer contenido entre paréntesis
    std::string colStr = extractParenContent(remaining);
    if (colStr.empty())
        return error("Falta la definición de columnas en CREATE TABLE.");

    // Validar que la tabla no exista ya
    if (catalogo.tableExists(dbContexto, tableName))
        return error("La tabla '" + tableName + "' ya existe en '" + dbContexto + "'.");

    // Parsear columnas
    std::vector<ColumnDefinition> cols;
    try {
        cols = parseColumnDefinitions(colStr);
    } catch (const std::exception& e) {
        return error(std::string("Error al parsear columnas: ") + e.what());
    }

    if (cols.empty())
        return error("La tabla debe tener al menos una columna.");

    // Construir la definición
    TableDefinition tableDef;
    tableDef.dbName    = dbContexto;
    tableDef.tableName = tableName;
    tableDef.columns   = cols;

    // Registrar en catálogo
    if (!catalogo.createTable(tableDef))
        return error("No se pudo registrar la tabla en el catálogo.");

    // Crear el archivo binario
    if (!dataManager.createTableFile(tableDef))
        return error("No se pudo crear el archivo de la tabla.");

    return ok("Tabla '" + tableName + "' creada exitosamente.");
}

// ============================================================
// DROP TABLE
// ============================================================

QueryResult ProcesadorDeConsultas::handleDropTable(const std::string& sql, const std::string& dbContexto) {
    if (dbContexto.empty())
        return error("No hay base de datos activa. Use SET DATABASE primero.");

    // DROP TABLE <name>
    std::string rest = trim(sql.substr(10)); // después de "DROP TABLE"
    if (rest.empty()) return error("Falta el nombre de la tabla.");

    std::string tableName = trim(rest);

    if (!catalogo.tableExists(dbContexto, tableName))
        return error("La tabla '" + tableName + "' no existe.");

    // Solo se puede eliminar si está vacía
    TableDefinition def = catalogo.getTableDef(dbContexto, tableName);
    if (!dataManager.isEmpty(def))
        return error("La tabla '" + tableName + "' no está vacía. Elimine los registros primero.");

    if (!catalogo.dropTable(dbContexto, tableName))
        return error("No se pudo eliminar la tabla del catálogo.");

    if (!dataManager.deleteTableFile(dbContexto, tableName))
        return error("No se pudo eliminar el archivo de la tabla.");

    return ok("Tabla '" + tableName + "' eliminada exitosamente.");
}

// ============================================================
// INSERT INTO
// ============================================================

QueryResult ProcesadorDeConsultas::handleInsert(const std::string& sql, const std::string& dbContexto) {
    if (dbContexto.empty())
        return error("No hay base de datos activa. Use SET DATABASE primero.");

    // INSERT INTO <table> VALUES(<vals>)
    // También acepta INSERT INTO <table> (<vals>) sin la palabra VALUES
    std::string upper = toUpper(sql);

    // Extraer nombre de tabla
    std::string afterInsert = trim(sql.substr(11)); // después de "INSERT INTO"
    std::istringstream iss(afterInsert);
    std::string tableName;
    iss >> tableName;

    if (!catalogo.tableExists(dbContexto, tableName))
        return error("La tabla '" + tableName + "' no existe.");

    TableDefinition def = catalogo.getTableDef(dbContexto, tableName);

    // Encontrar el paréntesis de valores
    // Puede ser: VALUES(...) o directamente (...)
    std::string remaining = afterInsert.substr(tableName.size());
    remaining = trim(remaining);

    std::string upperRem = toUpper(remaining);
    if (upperRem.rfind("VALUES", 0) == 0) {
        remaining = trim(remaining.substr(6));
    }

    std::string valStr = extractParenContent(remaining);
    if (valStr.empty())
        return error("Faltan los valores en INSERT INTO.");

    Row values;
    try {
        values = parseInsertValues(valStr);
    } catch (const std::exception& e) {
        return error(std::string("Error al parsear valores: ") + e.what());
    }

    // Validar cantidad de columnas
    if (values.size() != def.columns.size())
        return error("Número de valores (" + std::to_string(values.size()) +
                     ") no coincide con el número de columnas (" +
                     std::to_string(def.columns.size()) + ").");

    // Validar tipos de dato
    for (size_t i = 0; i < def.columns.size(); i++) {
        const std::string& val = values[i];
        const ColumnDefinition& col = def.columns[i];

        if (val == "NULL") {
            if (!col.nullable)
                return error("La columna '" + col.name + "' no admite NULL.");
            continue;
        }

        switch (col.type) {
            case ColumnType::INTEGER:
                try { std::stoi(val); }
                catch (...) { return error("Valor inválido para INTEGER en columna '" + col.name + "': " + val); }
                break;
            case ColumnType::DOUBLE:
                try { std::stod(val); }
                catch (...) { return error("Valor inválido para DOUBLE en columna '" + col.name + "': " + val); }
                break;
            case ColumnType::VARCHAR:
                if (val.size() > col.varcharSize)
                    return error("Valor demasiado largo para VARCHAR(" +
                                 std::to_string(col.varcharSize) + ") en columna '" + col.name + "'.");
                break;
            case ColumnType::DATETIME:
                // Validación básica de formato YYYY-MM-DD HH:MM:SS
                if (val.size() != 19)
                    return error("Formato de DATETIME inválido en columna '" + col.name +
                                 "'. Use: YYYY-MM-DD HH:MM:SS");
                break;
        }
    }

    // Verificar unicidad en columnas con índice
    auto indexes = catalogo.listIndexes();
    for (const auto& idx : indexes) {
        if (idx.dbName != dbContexto || idx.tableName != tableName) continue;

        // Encontrar el índice de la columna en la definición
        int colPos = -1;
        for (size_t i = 0; i < def.columns.size(); i++) {
            if (def.columns[i].name == idx.colName) { colPos = (int)i; break; }
        }
        if (colPos < 0) continue;

        // Buscar si ya existe ese valor
        std::string newVal = values[colPos];
        auto allRows = dataManager.scanAll(def);
        for (const auto& [offset, row] : allRows) {
            if (row[colPos] == newVal)
                return error("Valor duplicado '" + newVal + "' en columna indexada '" +
                             idx.colName + "'. El índice no permite duplicados.");
        }
    }

    // Escribir el registro
    try {
        dataManager.writeRecord(def, values);
    } catch (const std::exception& e) {
        return error(std::string("Error al escribir el registro: ") + e.what());
    }

    return ok("1 fila insertada en '" + tableName + "'.");
}

// ============================================================
// SELECT
// ============================================================

QueryResult ProcesadorDeConsultas::handleSelect(const std::string& sql, const std::string& dbContexto) {
    // SELECT * | col1, col2 FROM table [WHERE ...] [ORDER BY col asc|desc]

    // ---- Detectar SELECT sobre system catalog ----
    std::string upperSql = toUpper(sql);

    // Parsear FROM para obtener la tabla
    size_t fromPos = upperSql.find(" FROM ");
    if (fromPos == std::string::npos)
        return error("Falta la cláusula FROM en SELECT.");

    std::string colsPart = trim(sql.substr(6, fromPos - 6)); // entre SELECT y FROM
    std::string afterFrom = trim(sql.substr(fromPos + 6));

    // Separar tabla, WHERE y ORDER BY
    std::string tableName, whereClause, orderByClause;
    std::string upperAfterFrom = toUpper(afterFrom);

    size_t wherePos   = upperAfterFrom.find(" WHERE ");
    size_t orderPos   = upperAfterFrom.find(" ORDER BY ");

    if (wherePos != std::string::npos) {
        tableName    = trim(afterFrom.substr(0, wherePos));
        std::string afterWhere = trim(afterFrom.substr(wherePos + 7));

        // Buscar ORDER BY dentro del afterWhere
        std::string upperAfterWhere = toUpper(afterWhere);
        size_t orderInWhere = upperAfterWhere.find(" ORDER BY ");
        if (orderInWhere != std::string::npos) {
            whereClause   = trim(afterWhere.substr(0, orderInWhere));
            orderByClause = trim(afterWhere.substr(orderInWhere + 10));
        } else {
            whereClause = afterWhere;
        }
    } else if (orderPos != std::string::npos) {
        tableName     = trim(afterFrom.substr(0, orderPos));
        orderByClause = trim(afterFrom.substr(orderPos + 10));
    } else {
        tableName = trim(afterFrom);
    }

    // ---- System catalog queries ----
    std::string upperTable = toUpper(tableName);
    if (upperTable == "SYSTEMDATABASES" || upperTable == "SYSTEM_DATABASES") {
        auto dbs = catalogo.listDatabases();
        QueryResult res;
        res.success = true;
        res.message = std::to_string(dbs.size()) + " base(s) de datos.";
        res.columns = { "DatabaseName" };
        for (const auto& db : dbs)
            res.rows.push_back({ db });
        return res;
    }

    if (upperTable == "SYSTEMTABLES" || upperTable == "SYSTEM_TABLES") {
        std::string db = dbContexto;
        auto tables = catalogo.listTables(db);
        QueryResult res;
        res.success = true;
        res.columns = { "TableName", "Database" };
        for (const auto& t : tables)
            res.rows.push_back({ t, db });
        res.message = std::to_string(res.rows.size()) + " tabla(s).";
        return res;
    }

    if (upperTable == "SYSTEMINDEXES" || upperTable == "SYSTEM_INDEXES") {
        auto idxs = catalogo.listIndexes();
        QueryResult res;
        res.success = true;
        res.columns = { "IndexName", "Table", "Column", "Type" };
        for (const auto& idx : idxs)
            res.rows.push_back({ idx.indexName, idx.tableName, idx.colName, idx.type });
        res.message = std::to_string(res.rows.size()) + " índice(s).";
        return res;
    }

    // ---- Tabla normal ----
    if (dbContexto.empty())
        return error("No hay base de datos activa. Use SET DATABASE primero.");

    if (!catalogo.tableExists(dbContexto, tableName))
        return error("La tabla '" + tableName + "' no existe.");

    TableDefinition def = catalogo.getTableDef(dbContexto, tableName);

    // Determinar columnas a proyectar
    std::vector<int> projCols; // índices de columnas seleccionadas
    std::vector<std::string> projNames;

    std::string upperCols = toUpper(colsPart);
    if (trim(upperCols) == "*") {
        for (size_t i = 0; i < def.columns.size(); i++) {
            projCols.push_back((int)i);
            projNames.push_back(def.columns[i].name);
        }
    } else {
        auto colList = split(colsPart, ',');
        for (auto& cn : colList) {
            std::string cname = trim(cn);
            bool found = false;
            for (size_t i = 0; i < def.columns.size(); i++) {
                if (toUpper(def.columns[i].name) == toUpper(cname)) {
                    projCols.push_back((int)i);
                    projNames.push_back(def.columns[i].name);
                    found = true;
                    break;
                }
            }
            if (!found)
                return error("Columna '" + cname + "' no existe en la tabla '" + tableName + "'.");
        }
    }

    // Obtener filas (con o sin índice)
    std::vector<std::pair<int64_t, Row>> candidates;

    if (!whereClause.empty()) {
        // Parsear la condición WHERE
        WhereCondition cond;
        try {
            cond = parseWhereCondition(whereClause, def);
        } catch (const std::exception& e) {
            return error(std::string("Error en cláusula WHERE: ") + e.what());
        }

        // Intentar usar índice si la columna WHERE tiene uno
        bool usedIndex = false;
        auto indexes = catalogo.listIndexes();
        for (const auto& idx : indexes) {
            if (idx.dbName == dbContexto && idx.tableName == tableName &&
                toUpper(idx.colName) == toUpper(cond.colName) &&
                cond.op == "=") {
                // Búsqueda secuencial filtrada (el índice en memoria apuntaría al offset;
                // como BTree/BST aún no están implementados, hacemos scan y filtramos)
                auto all = dataManager.scanAll(def);
                for (auto& pair : all) {
                    if (evaluateCondition(pair.second, cond, def))
                        candidates.push_back(pair);
                }
                usedIndex = true;
                break;
            }
        }

        if (!usedIndex) {
            // Búsqueda secuencial
            auto all = dataManager.scanAll(def);
            for (auto& pair : all) {
                if (evaluateCondition(pair.second, cond, def))
                    candidates.push_back(pair);
            }
        }
    } else {
        candidates = dataManager.scanAll(def);
    }

    // Proyección
    Matrix resultRows;
    for (const auto& [offset, row] : candidates) {
        Row projected;
        for (int idx : projCols) {
            projected.push_back((idx < (int)row.size()) ? row[idx] : "");
        }
        resultRows.push_back(projected);
    }

    // ORDER BY
    if (!orderByClause.empty()) {
        // Formato: <column> [ASC|DESC]
        auto parts = split(orderByClause, ' ');
        // Filtrar tokens vacíos
        std::vector<std::string> tokens;
        for (auto& p : parts) {
            std::string t = trim(p);
            if (!t.empty()) tokens.push_back(t);
        }

        if (tokens.empty())
            return error("Falta el nombre de columna en ORDER BY.");

        std::string orderCol = tokens[0];
        bool descending = false;
        if (tokens.size() >= 2 && toUpper(tokens[1]) == "DESC")
            descending = true;

        // Encontrar índice de la columna de ordenamiento en las columnas proyectadas
        int orderIdx = -1;
        for (size_t i = 0; i < projNames.size(); i++) {
            if (toUpper(projNames[i]) == toUpper(orderCol)) {
                orderIdx = (int)i;
                break;
            }
        }
        // Si no está proyectada, buscarla en la definición original
        int orderColDefIdx = -1;
        if (orderIdx < 0) {
            for (size_t i = 0; i < def.columns.size(); i++) {
                if (toUpper(def.columns[i].name) == toUpper(orderCol)) {
                    orderColDefIdx = (int)i;
                    break;
                }
            }
            if (orderColDefIdx < 0)
                return error("Columna '" + orderCol + "' no existe en la tabla para ORDER BY.");

            // Necesitamos volver a obtener los datos con la columna extra para ordenar
            // Reordenar candidates directamente con la columna de def
            std::vector<std::pair<Row, Row>> extended; // {fullRow, projectedRow}
            size_t idx2 = 0;
            for (const auto& [offset, row] : candidates) {
                extended.push_back({ row, resultRows[idx2++] });
            }

            ColumnType colType = def.columns[orderColDefIdx].type;
            quicksort(extended, 0, (int)extended.size() - 1, orderColDefIdx, colType, descending, true);

            resultRows.clear();
            for (auto& [full, proj] : extended)
                resultRows.push_back(proj);
        } else {
            // Ordenar directamente por la columna proyectada
            ColumnType colType = ColumnType::VARCHAR; // default
            // Buscar el tipo real
            for (size_t i = 0; i < def.columns.size(); i++) {
                if (toUpper(def.columns[i].name) == toUpper(orderCol)) {
                    colType = def.columns[i].type;
                    break;
                }
            }

            // Adaptar: construir vector de pares para quicksort
            std::vector<std::pair<Row, Row>> extended;
            for (auto& row : resultRows)
                extended.push_back({ row, row });

            quicksort(extended, 0, (int)extended.size() - 1, orderIdx, colType, descending, false);

            resultRows.clear();
            for (auto& [full, proj] : extended)
                resultRows.push_back(proj);
        }
    }

    QueryResult res;
    res.success = true;
    res.columns = projNames;
    res.rows    = resultRows;
    res.message = std::to_string(resultRows.size()) + " fila(s) encontrada(s).";
    return res;
}

// ============================================================
// UPDATE
// ============================================================

QueryResult ProcesadorDeConsultas::handleUpdate(const std::string& sql, const std::string& dbContexto) {
    if (dbContexto.empty())
        return error("No hay base de datos activa. Use SET DATABASE primero.");

    // UPDATE <table> SET col = val [, col = val] [WHERE ...]
    std::string upper = toUpper(sql);

    // Extraer nombre de tabla
    std::string afterUpdate = trim(sql.substr(6)); // después de "UPDATE"
    std::istringstream iss(afterUpdate);
    std::string tableName;
    iss >> tableName;

    if (!catalogo.tableExists(dbContexto, tableName))
        return error("La tabla '" + tableName + "' no existe.");

    TableDefinition def = catalogo.getTableDef(dbContexto, tableName);

    // Buscar SET y WHERE
    std::string upperAfter = toUpper(afterUpdate);
    size_t setPos = upperAfter.find(" SET ");
    if (setPos == std::string::npos)
        return error("Falta la cláusula SET en UPDATE.");

    std::string afterSet = trim(afterUpdate.substr(setPos + 5));
    std::string upperAfterSet = toUpper(afterSet);

    std::string setClause, whereClause;
    size_t wherePos = upperAfterSet.find(" WHERE ");
    if (wherePos != std::string::npos) {
        setClause   = trim(afterSet.substr(0, wherePos));
        whereClause = trim(afterSet.substr(wherePos + 7));
    } else {
        setClause = afterSet;
    }

    // Parsear asignaciones: col1 = val1, col2 = val2
    struct Assignment { int colIdx; std::string value; };
    std::vector<Assignment> assignments;

    auto setParts = split(setClause, ',');
    for (auto& part : setParts) {
        std::string p = trim(part);
        size_t eqPos = p.find('=');
        if (eqPos == std::string::npos)
            return error("Sintaxis inválida en SET: " + p);

        std::string colName = trim(p.substr(0, eqPos));
        std::string colVal  = trim(p.substr(eqPos + 1));

        // Quitar comillas si las tiene
        if (colVal.size() >= 2 && colVal.front() == '"' && colVal.back() == '"')
            colVal = colVal.substr(1, colVal.size() - 2);

        int colIdx = -1;
        for (size_t i = 0; i < def.columns.size(); i++) {
            if (toUpper(def.columns[i].name) == toUpper(colName)) {
                colIdx = (int)i;
                break;
            }
        }
        if (colIdx < 0)
            return error("Columna '" + colName + "' no existe en la tabla '" + tableName + "'.");

        assignments.push_back({ colIdx, colVal });
    }

    // Obtener filas a actualizar
    std::vector<std::pair<int64_t, Row>> toUpdate;

    if (!whereClause.empty()) {
        WhereCondition cond;
        try {
            cond = parseWhereCondition(whereClause, def);
        } catch (const std::exception& e) {
            return error(std::string("Error en WHERE: ") + e.what());
        }
        auto all = dataManager.scanAll(def);
        for (auto& pair : all) {
            if (evaluateCondition(pair.second, cond, def))
                toUpdate.push_back(pair);
        }
    } else {
        toUpdate = dataManager.scanAll(def);
    }

    // Validar unicidad en columnas indexadas que se van a actualizar
    auto indexes = catalogo.listIndexes();
    for (const auto& asgn : assignments) {
        for (const auto& idx : indexes) {
            if (idx.dbName != dbContexto || idx.tableName != tableName) continue;
            if (toUpper(idx.colName) != toUpper(def.columns[asgn.colIdx].name)) continue;

            // Verificar que el nuevo valor no exista ya en otra fila
            auto allRows = dataManager.scanAll(def);
            for (const auto& [offset, row] : allRows) {
                // Si este offset ya está en toUpdate, es una fila que vamos a cambiar — ok
                bool isBeingUpdated = false;
                for (const auto& [upOffset, upRow] : toUpdate) {
                    if (upOffset == offset) { isBeingUpdated = true; break; }
                }
                if (isBeingUpdated) continue;

                if (row[asgn.colIdx] == asgn.value)
                    return error("Valor duplicado '" + asgn.value + "' en columna indexada '" +
                                 idx.colName + "'.");
            }
        }
    }

    // Aplicar actualizaciones
    int count = 0;
    for (const auto& [offset, row] : toUpdate) {
        Row newRow = row;
        for (const auto& asgn : assignments)
            newRow[asgn.colIdx] = asgn.value;

        if (!dataManager.updateRecord(def, offset, newRow))
            return error("Error al actualizar fila en offset " + std::to_string(offset));
        count++;
    }

    return ok(std::to_string(count) + " fila(s) actualizada(s) en '" + tableName + "'.");
}

// ============================================================
// DELETE
// ============================================================

QueryResult ProcesadorDeConsultas::handleDelete(const std::string& sql, const std::string& dbContexto) {
    if (dbContexto.empty())
        return error("No hay base de datos activa. Use SET DATABASE primero.");

    // DELETE FROM <table> [WHERE ...]
    std::string upper = toUpper(sql);

    size_t fromPos = upper.find(" FROM ");
    if (fromPos == std::string::npos)
        return error("Falta FROM en DELETE.");

    std::string afterFrom = trim(sql.substr(fromPos + 6));
    std::string upperAfterFrom = toUpper(afterFrom);

    std::string tableName, whereClause;
    size_t wherePos = upperAfterFrom.find(" WHERE ");
    if (wherePos != std::string::npos) {
        tableName   = trim(afterFrom.substr(0, wherePos));
        whereClause = trim(afterFrom.substr(wherePos + 7));
    } else {
        tableName = trim(afterFrom);
    }

    if (!catalogo.tableExists(dbContexto, tableName))
        return error("La tabla '" + tableName + "' no existe.");

    TableDefinition def = catalogo.getTableDef(dbContexto, tableName);

    // Obtener filas a eliminar
    std::vector<std::pair<int64_t, Row>> toDelete;

    if (!whereClause.empty()) {
        WhereCondition cond;
        try {
            cond = parseWhereCondition(whereClause, def);
        } catch (const std::exception& e) {
            return error(std::string("Error en WHERE: ") + e.what());
        }
        auto all = dataManager.scanAll(def);
        for (auto& pair : all) {
            if (evaluateCondition(pair.second, cond, def))
                toDelete.push_back(pair);
        }
    } else {
        toDelete = dataManager.scanAll(def);
    }

    // Eliminar marcando lógicamente
    int count = 0;
    for (const auto& [offset, row] : toDelete) {
        if (!dataManager.deleteRecord(def, offset))
            return error("Error al eliminar fila en offset " + std::to_string(offset));
        count++;
    }

    return ok(std::to_string(count) + " fila(s) eliminada(s) de '" + tableName + "'.");
}

// ============================================================
// CREATE INDEX
// ============================================================

QueryResult ProcesadorDeConsultas::handleCreateIndex(const std::string& sql, const std::string& dbContexto) {
    if (dbContexto.empty())
        return error("No hay base de datos activa. Use SET DATABASE primero.");

    // CREATE INDEX <name> ON <table>(<col>) OF TYPE <BTREE|BST>
    std::string upper = toUpper(sql);

    // Extraer nombre del índice
    std::string afterCI = trim(sql.substr(12)); // después de "CREATE INDEX"
    std::istringstream iss(afterCI);
    std::string indexName;
    iss >> indexName;

    std::string upperAfterCI = toUpper(afterCI);

    // Buscar "ON"
    size_t onPos = upperAfterCI.find(" ON ");
    if (onPos == std::string::npos)
        return error("Falta la cláusula ON en CREATE INDEX.");

    std::string afterOn = trim(afterCI.substr(onPos + 4));
    std::string upperAfterOn = toUpper(afterOn);

    // Buscar "OF TYPE"
    size_t ofTypePos = upperAfterOn.find(" OF TYPE ");
    if (ofTypePos == std::string::npos)
        return error("Falta la cláusula OF TYPE en CREATE INDEX.");

    std::string tableAndCol = trim(afterOn.substr(0, ofTypePos));
    std::string indexType   = trim(afterOn.substr(ofTypePos + 9));
    indexType = toUpper(indexType);

    if (indexType != "BTREE" && indexType != "BST")
        return error("Tipo de índice inválido. Use BTREE o BST.");

    // Extraer tabla y columna: Tabla(columna)
    size_t parenOpen = tableAndCol.find('(');
    if (parenOpen == std::string::npos)
        return error("Sintaxis inválida en CREATE INDEX. Use: tabla(columna)");

    std::string tableName = trim(tableAndCol.substr(0, parenOpen));
    std::string colName   = extractParenContent(tableAndCol);

    if (!catalogo.tableExists(dbContexto, tableName))
        return error("La tabla '" + tableName + "' no existe.");

    TableDefinition def = catalogo.getTableDef(dbContexto, tableName);

    // Validar que la columna exista
    int colIdx = -1;
    for (size_t i = 0; i < def.columns.size(); i++) {
        if (toUpper(def.columns[i].name) == toUpper(colName)) {
            colIdx = (int)i;
            break;
        }
    }
    if (colIdx < 0)
        return error("La columna '" + colName + "' no existe en la tabla '" + tableName + "'.");

    // Verificar que no exista ya un índice en esa columna
    if (catalogo.indexExists(tableName, colName))
        return error("Ya existe un índice en la columna '" + colName + "' de '" + tableName + "'.");

    // Si la tabla ya tiene datos, verificar que no haya duplicados
    auto allRows = dataManager.scanAll(def);
    if (!allRows.empty()) {
        std::vector<std::string> seen;
        for (const auto& [offset, row] : allRows) {
            const std::string& val = row[colIdx];
            for (const auto& s : seen) {
                if (s == val)
                    return error("No se puede crear el índice: valor duplicado '" + val +
                                 "' en columna '" + colName + "'.");
            }
            seen.push_back(val);
        }
    }

    // Registrar en el catálogo
    IndexDefinition idx;
    idx.indexName = indexName;
    idx.tableName = tableName;
    idx.dbName    = dbContexto;
    idx.colName   = colName;
    idx.type      = indexType;

    if (!catalogo.addIndex(idx))
        return error("No se pudo registrar el índice en el catálogo.");

    return ok("Índice '" + indexName + "' creado exitosamente sobre '" +
              tableName + "." + colName + "' (" + indexType + ").");
}

// ============================================================
// Helpers de parsing
// ============================================================

std::string ProcesadorDeConsultas::toUpper(const std::string& s) const {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c){ return std::toupper(c); });
    return r;
}

std::string ProcesadorDeConsultas::trim(const std::string& s) const {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::vector<std::string> ProcesadorDeConsultas::split(const std::string& s, char delim) const {
    std::vector<std::string> tokens;
    std::string token;
    bool inQuotes = false;
    int parenDepth = 0;

    for (char c : s) {
        if (c == '"') inQuotes = !inQuotes;
        else if (!inQuotes && c == '(') parenDepth++;
        else if (!inQuotes && c == ')') parenDepth--;

        if (!inQuotes && parenDepth == 0 && c == delim) {
            tokens.push_back(token);
            token.clear();
        } else {
            token += c;
        }
    }
    if (!token.empty()) tokens.push_back(token);
    return tokens;
}

std::string ProcesadorDeConsultas::extractParenContent(const std::string& s) const {
    size_t open = s.find('(');
    if (open == std::string::npos) return "";
    size_t close = s.rfind(')');
    if (close == std::string::npos || close <= open) return "";
    return trim(s.substr(open + 1, close - open - 1));
}

std::vector<ColumnDefinition> ProcesadorDeConsultas::parseColumnDefinitions(const std::string& colStr) const {
    std::vector<ColumnDefinition> cols;
    auto parts = split(colStr, ',');

    uint32_t idx = 0;
    for (auto& part : parts) {
        std::string p = trim(part);
        if (p.empty()) continue;

        std::istringstream ss(p);
        std::string name, typeStr;
        ss >> name >> typeStr;

        ColumnDefinition col;
        col.name      = name;
        col.nullable  = true;  // Por defecto nullable
        col.varcharSize = 0;
        col.colIndex  = idx++;

        std::string upperType = toUpper(typeStr);

        // Leer el resto de la línea para NOT NULL u otros modificadores
        std::string rest;
        std::getline(ss, rest);
        rest = trim(rest);
        std::string upperRest = toUpper(rest);
        if (upperRest.find("NOT NULL") != std::string::npos)
            col.nullable = false;

        if (upperType == "INTEGER") {
            col.type = ColumnType::INTEGER;
        } else if (upperType == "DOUBLE") {
            col.type = ColumnType::DOUBLE;
        } else if (upperType == "DATETIME") {
            col.type = ColumnType::DATETIME;
        } else if (upperType.rfind("VARCHAR", 0) == 0) {
            col.type = ColumnType::VARCHAR;
            // Extraer el tamaño de VARCHAR(N) — puede venir pegado al tipo o separado
            std::string varPart = typeStr; // original con case
            // Buscar paréntesis en typeStr
            size_t op = varPart.find('(');
            if (op != std::string::npos) {
                size_t cp = varPart.find(')');
                if (cp != std::string::npos) {
                    col.varcharSize = std::stoul(varPart.substr(op + 1, cp - op - 1));
                }
            } else {
                // El tamaño puede estar en rest: VARCHAR 30 o VARCHAR(30) en rest
                if (!rest.empty() && rest[0] == '(') {
                    std::string sizeStr = extractParenContent(rest);
                    col.varcharSize = std::stoul(sizeStr);
                } else {
                    col.varcharSize = 255; // default
                }
            }
            if (col.varcharSize == 0)
                throw std::runtime_error("VARCHAR debe tener un tamaño mayor a 0.");
        } else {
            throw std::runtime_error("Tipo de columna desconocido: " + typeStr);
        }

        cols.push_back(col);
    }

    return cols;
}

Row ProcesadorDeConsultas::parseInsertValues(const std::string& valStr) const {
    Row values;
    // Dividir por comas respetando las comillas
    bool inQuotes = false;
    std::string current;

    for (size_t i = 0; i < valStr.size(); i++) {
        char c = valStr[i];
        if (c == '"') {
            inQuotes = !inQuotes;
            // No incluir las comillas en el valor
        } else if (c == ',' && !inQuotes) {
            values.push_back(trim(current));
            current.clear();
        } else {
            current += c;
        }
    }
    values.push_back(trim(current));

    return values;
}

// ============================================================
// WHERE parsing y evaluación
// ============================================================

ProcesadorDeConsultas::WhereCondition ProcesadorDeConsultas::parseWhereCondition(
    const std::string& whereStr, const TableDefinition& def) const
{
    WhereCondition cond;
    std::string s = trim(whereStr);
    std::string upper = toUpper(s);

    // Operadores en orden de mayor a menor longitud para evitar matches parciales
    const std::vector<std::string> ops = { ">=", "<=", "<>", "!=", "LIKE", "NOT", ">", "<", "=", "==" };

    for (const auto& op : ops) {
        size_t pos = upper.find(op);
        if (pos == std::string::npos) continue;

        cond.colName = trim(s.substr(0, pos));
        cond.op      = op;
        cond.value   = trim(s.substr(pos + op.size()));

        // Quitar comillas del valor
        if (cond.value.size() >= 2 &&
            cond.value.front() == '"' && cond.value.back() == '"') {
            cond.value = cond.value.substr(1, cond.value.size() - 2);
        }

        // Normalizar == a =
        if (cond.op == "==") cond.op = "=";

        // Validar que la columna existe
        bool found = false;
        for (const auto& col : def.columns) {
            if (toUpper(col.name) == toUpper(cond.colName)) {
                found = true;
                cond.colType = col.type;
                break;
            }
        }
        if (!found)
            throw std::runtime_error("Columna '" + cond.colName + "' no existe en la tabla.");

        return cond;
    }

    throw std::runtime_error("Condición WHERE no reconocida: " + whereStr);
}

bool ProcesadorDeConsultas::evaluateCondition(
    const Row& row,
    const WhereCondition& cond,
    const TableDefinition& def) const
{
    // Encontrar el índice de la columna
    int colIdx = -1;
    for (size_t i = 0; i < def.columns.size(); i++) {
        if (toUpper(def.columns[i].name) == toUpper(cond.colName)) {
            colIdx = (int)i;
            break;
        }
    }
    if (colIdx < 0 || colIdx >= (int)row.size()) return false;

    const std::string& cellVal = row[colIdx];
    const std::string& condVal = cond.value;

    // LIKE: soporta * como wildcard (ej: *mire* = contiene "mire")
    if (cond.op == "LIKE") {
        std::string pattern = condVal;
        std::string cell    = toUpper(cellVal);
        std::string pat     = toUpper(pattern);

        // Convertir * a regex .*
        std::string regexPat;
        for (char c : pat) {
            if (c == '*') regexPat += ".*";
            else if (std::ispunct(c) && c != '_') { regexPat += '\\'; regexPat += c; }
            else regexPat += c;
        }
        try {
            std::regex re(regexPat);
            return std::regex_match(cell, re);
        } catch (...) {
            return false;
        }
    }

    // NOT: distinto de
    if (cond.op == "NOT") {
        return toUpper(cellVal) != toUpper(condVal);
    }

    // Comparaciones numéricas o de string según el tipo
    if (cond.colType == ColumnType::INTEGER) {
        try {
            int a = std::stoi(cellVal);
            int b = std::stoi(condVal);
            if (cond.op == "=")  return a == b;
            if (cond.op == ">")  return a >  b;
            if (cond.op == "<")  return a <  b;
            if (cond.op == ">=") return a >= b;
            if (cond.op == "<=") return a <= b;
            if (cond.op == "<>" || cond.op == "!=") return a != b;
        } catch (...) { return false; }
    } else if (cond.colType == ColumnType::DOUBLE) {
        try {
            double a = std::stod(cellVal);
            double b = std::stod(condVal);
            if (cond.op == "=")  return a == b;
            if (cond.op == ">")  return a >  b;
            if (cond.op == "<")  return a <  b;
            if (cond.op == ">=") return a >= b;
            if (cond.op == "<=") return a <= b;
            if (cond.op == "<>" || cond.op == "!=") return a != b;
        } catch (...) { return false; }
    } else {
        // VARCHAR / DATETIME: comparación lexicográfica
        if (cond.op == "=")  return cellVal == condVal;
        if (cond.op == ">")  return cellVal >  condVal;
        if (cond.op == "<")  return cellVal <  condVal;
        if (cond.op == ">=") return cellVal >= condVal;
        if (cond.op == "<=") return cellVal <= condVal;
        if (cond.op == "<>" || cond.op == "!=") return cellVal != condVal;
    }

    return false;
}

// ============================================================
// Quicksort para ORDER BY
// ============================================================

void ProcesadorDeConsultas::quicksort(
    std::vector<std::pair<Row, Row>>& data,
    int lo, int hi,
    int colIdx,
    ColumnType colType,
    bool descending,
    bool useFirst) const
{
    if (lo >= hi) return;
    int p = partition(data, lo, hi, colIdx, colType, descending, useFirst);
    quicksort(data, lo,  p - 1, colIdx, colType, descending, useFirst);
    quicksort(data, p + 1, hi,  colIdx, colType, descending, useFirst);
}

int ProcesadorDeConsultas::partition(
    std::vector<std::pair<Row, Row>>& data,
    int lo, int hi,
    int colIdx,
    ColumnType colType,
    bool descending,
    bool useFirst) const
{
    auto getVal = [&](const std::pair<Row, Row>& p) -> const std::string& {
        return useFirst ? p.first[colIdx] : p.second[colIdx];
    };

    const std::string& pivot = getVal(data[hi]);
    int i = lo - 1;

    for (int j = lo; j < hi; j++) {
        bool less = compareValues(getVal(data[j]), pivot, colType);
        if (descending) less = compareValues(pivot, getVal(data[j]), colType);

        if (less) {
            i++;
            std::swap(data[i], data[j]);
        }
    }
    std::swap(data[i + 1], data[hi]);
    return i + 1;
}

bool ProcesadorDeConsultas::compareValues(
    const std::string& a,
    const std::string& b,
    ColumnType colType) const
{
    switch (colType) {
        case ColumnType::INTEGER:
            try { return std::stoi(a) < std::stoi(b); } catch (...) { return a < b; }
        case ColumnType::DOUBLE:
            try { return std::stod(a) < std::stod(b); } catch (...) { return a < b; }
        default:
            return a < b;
    }
}

// ============================================================
// Helpers de resultado
// ============================================================

QueryResult ProcesadorDeConsultas::error(const std::string& mensaje) const {
    QueryResult r;
    r.success = false;
    r.message = mensaje;
    return r;
}

QueryResult ProcesadorDeConsultas::ok(const std::string& mensaje) const {
    QueryResult r;
    r.success = true;
    r.message = mensaje;
    return r;
}