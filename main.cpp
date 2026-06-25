//
// main.cpp
// Motor de base de datos TinySQLDb
//
// Orden de arranque:
//   1. Crear SistemaDeCalogo  →  carga metadatos del disco a memoria
//   2. Crear DataManager      →  gestiona archivos .bin de las tablas
//   3. Crear ProcesadorDeConsultas →  cerebro que parsea y ejecuta SQL
//   4. Crear WebApi           →  expone el endpoint REST y bloquea
//

#include <iostream>
#include <filesystem>
#include <csignal>

#include "SistemaDeCatalogo.h"
#include "DataManager.h"
#include "ProcesadorDeConsultas.h"
#include "WebApi.h"

namespace fs = std::filesystem;

// ============================================================
//  Rutas de datos
//  Ajusta DATA_ROOT si quieres guardar los archivos en otro lugar.
//  Al ejecutar desde el directorio build/, los datos quedan en build/../data/
// ============================================================
static const std::string DATA_ROOT    = "../data";
static const std::string CATALOG_PATH = DATA_ROOT + "/system_catalog";

// Puerto del servidor (puede sobreescribirse con argv[1])
static const int DEFAULT_PORT = 8080;

// ============================================================
//  Puntero global al WebApi para poder detenerlo con Ctrl+C
// ============================================================
static WebApi* g_webApi = nullptr;

void signalHandler(int signal) {
    std::cout << "\n[INFO] Señal " << signal
              << " recibida. Deteniendo servidor...\n";
    if (g_webApi) g_webApi->stop();
    std::exit(0);
}

// ============================================================
//  main
// ============================================================
int main(int argc, char* argv[]) {

    // ---- 0. Puerto opcional por argumento ----
    int port = DEFAULT_PORT;
    if (argc >= 2) {
        try {
            port = std::stoi(argv[1]);
        } catch (...) {
            std::cerr << "[WARN] Puerto inválido '" << argv[1]
                      << "', usando " << DEFAULT_PORT << ".\n";
            port = DEFAULT_PORT;
        }
    }

    // ---- 1. Crear carpetas raíz si no existen ----
    try {
        fs::create_directories(DATA_ROOT);
        fs::create_directories(CATALOG_PATH);
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] No se pudo crear la estructura de carpetas: "
                  << e.what() << "\n";
        return 1;
    }

    std::cout << "=== TinySQLDb iniciando ===\n";
    std::cout << "  Datos en     : " << fs::absolute(DATA_ROOT)    << "\n";
    std::cout << "  Catálogo en  : " << fs::absolute(CATALOG_PATH) << "\n";

    // ---- 2. Sistema de Catálogo ----
    SistemaDeCatalogo catalogo(CATALOG_PATH);
    try {
        catalogo.load();
        std::cout << "  Catálogo     : OK — "
                  << catalogo.listDatabases().size() << " base(s) cargada(s)\n";
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Fallo al cargar el catálogo: " << e.what() << "\n";
        return 1;
    }

    // ---- 3. Data Manager ----
    DataManager dataManager(DATA_ROOT);
    std::cout << "  DataManager  : OK\n";

    // ---- 4. Procesador de Consultas ----
    ProcesadorDeConsultas procesador(catalogo, dataManager);
    std::cout << "  Procesador   : OK\n";

    // ---- 5. Manejo de señales (Ctrl+C) ----
    std::signal(SIGINT,  signalHandler);
    std::signal(SIGTERM, signalHandler);

    // ---- 6. Web API (bloquea hasta Ctrl+C) ----
    WebApi webApi(procesador, port);
    g_webApi = &webApi;

    std::cout << "\nPresiona Ctrl+C para detener el servidor.\n\n";
    webApi.run();   // <-- bloquea aquí

    return 0;
}