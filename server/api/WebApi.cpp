//
// WebApi.cpp
// Motor de base de datos TinySQLDb
//

#include "WebApi.h"
#include "httplib.h"   // cpp-httplib (header-only) – coloca httplib.h junto a este archivo
#include <chrono>
#include <iostream>
#include <sstream>

// ============================================================
//  Constructor
// ============================================================

WebApi::WebApi(ProcesadorDeConsultas& procesador, int port)
    : procesador(procesador), port(port) {}

// ============================================================
//  run()  —  registra rutas y bloquea
// ============================================================

void WebApi::run() {
    httplib::Server svr;

    // ----------------------------------------------------------
    // CORS: permite peticiones desde cualquier origen (el cliente
    // React corre en localhost:5173 o similar durante desarrollo).
    // ----------------------------------------------------------
    auto setCors = [](httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin",  "*");
        res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
    };

    // OPTIONS /query  —  preflight CORS
    svr.Options("/query", [setCors](const httplib::Request&, httplib::Response& res) {
        setCors(res);
        res.status = 204;
    });

    // ----------------------------------------------------------
    // POST /query
    // Body esperado (JSON):
    //   { "sql": "SELECT ...", "database": "MiDB" }
    // ----------------------------------------------------------
    svr.Post("/query", [this, setCors](const httplib::Request& req, httplib::Response& res) {
        setCors(res);
        res.set_header("Content-Type", "application/json");

        // 1. Extraer campos del JSON recibido
        std::string sql      = jsonGetString(req.body, "sql");
        std::string database = jsonGetString(req.body, "database");

        if (sql.empty()) {
            QueryResult err;
            err.success = false;
            err.message = "El campo 'sql' es obligatorio y no puede estar vacío.";
            res.body   = toJson(err);
            res.status = 400;
            return;
        }

        // 2. Ejecutar y medir tiempo
        auto t0 = std::chrono::high_resolution_clock::now();

        QueryResult result = procesador.ejecutar(sql, database);

        auto t1 = std::chrono::high_resolution_clock::now();
        result.time_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        // 3. Serializar y responder
        res.body   = toJson(result);
        res.status = 200;

        // Log mínimo en consola para depuración
        std::cout << "[" << (result.success ? "OK " : "ERR") << "] "
                  << sql.substr(0, 60) << (sql.size() > 60 ? "..." : "")
                  << "  (" << result.time_ms << " ms)\n";
    });

    // ----------------------------------------------------------
    // Inicio
    // ----------------------------------------------------------
    std::cout << "=== TinySQLDb WebApi escuchando en http://localhost:"
              << port << " ===\n";

    if (!svr.listen("0.0.0.0", port)) {
        std::cerr << "[ERROR] No se pudo iniciar el servidor en el puerto "
                  << port << ".\n"
                  << "  Verifica que el puerto no esté ocupado "
                     "(lsof -i :" << port << ")\n";
    }
}

void WebApi::stop() {
    // httplib::Server::stop() detiene el listen() bloqueante
    // Si necesitas detenerlo desde otra hebra, guarda una referencia al svr.
    // Por simplicidad, el proyecto usa Ctrl+C (SIGINT) para detener el proceso.
}

// ============================================================
//  JSON helpers (sin dependencias externas)
// ============================================================

std::string WebApi::jsonEscapeString(const std::string& s) const {
    std::ostringstream out;
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n";  break;
            case '\r': out << "\\r";  break;
            case '\t': out << "\\t";  break;
            default:
                if (c < 0x20) {
                    // Caracteres de control → \uXXXX
                    out << "\\u" << std::hex << std::uppercase
                        << (int)c << std::dec;
                } else {
                    out << c;
                }
        }
    }
    return out.str();
}

std::string WebApi::toJson(const QueryResult& r) const {
    std::ostringstream j;

    j << "{\n";
    j << "  \"status\": \"" << (r.success ? "ok" : "error") << "\",\n";
    j << "  \"message\": \"" << jsonEscapeString(r.message) << "\",\n";
    j << "  \"time_ms\": " << r.time_ms << ",\n";

    // columns: ["col1","col2",...]
    j << "  \"columns\": [";
    for (size_t i = 0; i < r.columns.size(); i++) {
        if (i) j << ", ";
        j << "\"" << jsonEscapeString(r.columns[i]) << "\"";
    }
    j << "],\n";

    // rows: [["v1","v2"],[...],...]
    j << "  \"rows\": [";
    for (size_t i = 0; i < r.rows.size(); i++) {
        if (i) j << ", ";
        j << "[";
        for (size_t k = 0; k < r.rows[i].size(); k++) {
            if (k) j << ", ";
            j << "\"" << jsonEscapeString(r.rows[i][k]) << "\"";
        }
        j << "]";
    }
    j << "]\n";

    j << "}";
    return j.str();
}

// Parser JSON minimalista: busca "key": "value" con comillas.
// Suficiente para el único JSON que recibe el servidor:
//   { "sql": "...", "database": "..." }
std::string WebApi::jsonGetString(const std::string& json,
                                  const std::string& key) const {
    // Buscar: "key":
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return "";

    // Avanzar hasta la primera comilla del valor
    pos = json.find('"', pos + needle.size());
    // Saltar el ':' y espacios antes de la comilla ya encontrada – ya estamos en él
    if (pos == std::string::npos) return "";
    pos++; // saltar la comilla de apertura

    // Leer hasta la comilla de cierre respetando escapes \"
    std::string result;
    while (pos < json.size()) {
        char c = json[pos];
        if (c == '\\' && pos + 1 < json.size()) {
            char next = json[pos + 1];
            switch (next) {
                case '"':  result += '"';  break;
                case '\\': result += '\\'; break;
                case 'n':  result += '\n'; break;
                case 'r':  result += '\r'; break;
                case 't':  result += '\t'; break;
                default:   result += next; break;
            }
            pos += 2;
        } else if (c == '"') {
            break; // comilla de cierre
        } else {
            result += c;
            pos++;
        }
    }
    return result;
}