//
// Created by nacho on 6/2/2026.
//

#ifndef MYMINISQL_WEBAPI_H
#define MYMINISQL_WEBAPI_H

//
// WebApi.h
// Motor de base de datos TinySQLDb
//
// Expone un endpoint REST POST /query que recibe:
//   { "sql": "...", "database": "..." }
// y responde con:
//   { "status": "ok"|"error", "message": "...",
//     "columns": [...], "rows": [[...]], "time_ms": N }
//
// Depende de cpp-httplib (header-only): https://github.com/yhirose/cpp-httplib
// Coloca httplib.h en el mismo directorio o en un path incluido.

#include "ProcesadorDeConsultas.h"
#include <string>

class WebApi {
public:
    // Construye el WebApi con el procesador de consultas ya inicializado.
    // port: puerto TCP en el que escuchará (por defecto 8080).
    WebApi(ProcesadorDeConsultas& procesador, int port = 8080);

    // Registra los endpoints y bloquea hasta que el servidor se detenga.
    void run();

    // Detiene el servidor (útil para pruebas o señales del SO).
    void stop();

private:
    ProcesadorDeConsultas& procesador;
    int port;

    // ----------------------------------------------------------------
    // JSON mínimo — sin dependencias externas
    // ----------------------------------------------------------------

    // Escapa caracteres especiales dentro de un valor string JSON.
    std::string jsonEscapeString(const std::string& s) const;

    // Serializa un QueryResult completo a JSON.
    std::string toJson(const QueryResult& result) const;

    // Extrae el valor de una clave string de un JSON plano.
    // Solo maneja el subconjunto que el cliente envía:
    //   { "sql": "...", "database": "..." }
    std::string jsonGetString(const std::string& json,
                              const std::string& key) const;
};

#endif //MYMINISQL_WEBAPI_H