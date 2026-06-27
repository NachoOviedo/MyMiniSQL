cmake_minimum_required(VERSION 3.16)
project(TinySQLDb VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
        httplib
        GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
        GIT_TAG        v0.18.1
)
FetchContent_MakeAvailable(httplib)

# common: solo Types.h, no tiene .cpp
add_library(common INTERFACE)
target_include_directories(common INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/server/common
)

add_library(storage STATIC server/Almacenamiento/DataManager.cpp)
target_include_directories(storage PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/server/Almacenamiento
        ${CMAKE_CURRENT_SOURCE_DIR}/server/common
)
target_link_libraries(storage PRIVATE common)

add_library(catalog STATIC server/Catalogo/SistemaDeCatalogo.cpp)
target_include_directories(catalog PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/server/Catalogo
        ${CMAKE_CURRENT_SOURCE_DIR}/server/common
)
target_link_libraries(catalog PRIVATE storage common)

add_library(indexes STATIC
        server/indexes/BST.cpp
        server/indexes/Btree.cpp
)
target_include_directories(indexes PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/server/indexes
        ${CMAKE_CURRENT_SOURCE_DIR}/server/common
)
target_link_libraries(indexes PRIVATE common)

add_library(query STATIC server/Consultas/ProcesadorDeConsultas.cpp)
target_include_directories(query PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/server/Consultas
        ${CMAKE_CURRENT_SOURCE_DIR}/server/common
)
target_link_libraries(query PUBLIC catalog storage indexes common)

add_library(api STATIC server/api/WebApi.cpp)
target_include_directories(api PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/server/api
        ${CMAKE_CURRENT_SOURCE_DIR}/server/common
)
target_link_libraries(api PRIVATE query httplib::httplib common)

add_executable(tinysqldb main.cpp)
target_include_directories(tinysqldb PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/server/Almacenamiento
        ${CMAKE_CURRENT_SOURCE_DIR}/server/Catalogo
        ${CMAKE_CURRENT_SOURCE_DIR}/server/Consultas
        ${CMAKE_CURRENT_SOURCE_DIR}/server/api
        ${CMAKE_CURRENT_SOURCE_DIR}/server/common
)
target_link_libraries(tinysqldb PRIVATE api query catalog storage indexes common)

# httplib usa la API de Winsock (socket, bind, listen, etc.) en Windows.
# Con MSVC el propio header se auto-enlaza via #pragma comment(lib, "Ws2_32.lib"),
# pero ese pragma NO funciona con MinGW/GCC: hay que enlazar ws2_32 explicitamente
# o el .exe falla al arrancar con STATUS_ENTRYPOINT_NOT_FOUND (0xC0000139).
if(WIN32)
    target_link_libraries(tinysqldb PRIVATE ws2_32)
endif()

# ------------------------------------------------------------------
# Copia automática de las DLLs de runtime de MinGW (mingw13) junto
# al .exe despues de cada build. Necesario porque en esta máquina
# coexisten varias instalaciones de MinGW (mingw13 y msys64) y el
# loader de Windows puede resolver versiones incompatibles desde el
# PATH, causando STATUS_ENTRYPOINT_NOT_FOUND (0xC0000139) al arrancar.
# Si mingw13 está en otra ruta en tu máquina, ajusta MINGW_BIN_DIR.
# ------------------------------------------------------------------
if(WIN32)
    set(MINGW_BIN_DIR "C:/Program Files/JetBrains/CLion 2026.1.1/bin/mingw/bin")
    set(MINGW_RUNTIME_DLLS
            "${MINGW_BIN_DIR}/libstdc++-6.dll"
            "${MINGW_BIN_DIR}/libgcc_s_seh-1.dll"
            "${MINGW_BIN_DIR}/libwinpthread-1.dll"
    )
    foreach(dll ${MINGW_RUNTIME_DLLS})
        add_custom_command(TARGET tinysqldb POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${dll}" "$<TARGET_FILE_DIR:tinysqldb>"
        )
    endforeach()
endif()