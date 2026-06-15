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

add_library(common INTERFACE
        server/indexes/BST.h)
target_include_directories(common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/common)

add_library(storage STATIC Almacenamiento/DataManager.cpp
        server/indexes/BST.h)
target_include_directories(storage PUBLIC Almacenamiento)
target_link_libraries(storage PRIVATE common)

add_library(catalog STATIC Catalogo/SistemaDeCalogo.cpp
        server/indexes/BST.h)
target_include_directories(catalog PUBLIC Catalogo)
target_link_libraries(catalog PRIVATE storage common)

add_library(indexes STATIC indexes/Btree.cpp
        server/indexes/BST.h)
target_include_directories(indexes PUBLIC indexes)
target_link_libraries(indexes PRIVATE common)

add_library(query STATIC Consultas/ProcesadorDeConsultas.cpp
        server/indexes/BST.h)
target_include_directories(query PUBLIC Consultas)
target_link_libraries(query PRIVATE catalog storage indexes common)

add_library(api STATIC api/WebApi.cpp
        server/indexes/BST.h
        server/indexes/BST.h)
target_include_directories(api PUBLIC api)
target_link_libraries(api PRIVATE query httplib::httplib common)

add_executable(tinysqldb main.cpp
        server/indexes/BST.h)
target_link_libraries(tinysqldb PRIVATE api query catalog storage indexes common)