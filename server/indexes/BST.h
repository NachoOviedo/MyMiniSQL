//
// BST.h
// Árbol Binario de Búsqueda para índices de TinySQLDb
//
// ¿Qué hace este árbol?
//   Guarda pares (clave → offset en disco).
//   Cuando el query processor necesita encontrar un registro donde
//   columna = valor, le pregunta al BST: "¿en qué posición del archivo
//   está ese valor?" y va directo ahí sin leer todo el archivo.
//
// Estructura de un nodo:
//
//         [clave="3", offset=12000]
//              /              \
//   [clave="1", offset=0]   [clave="5", offset=24000]
//
// Regla del BST: todo lo que está a la izquierda tiene clave menor,
//               todo lo que está a la derecha tiene clave mayor.

#ifndef TINYSQLDB_BST_H
#define TINYSQLDB_BST_H

#include <string>
#include <vector>
#include <functional>
#include <cstdint>

// Un resultado de búsqueda devuelve el offset en disco del registro
struct BSTSearchResult {
    bool found;    // ¿Se encontró la clave?
    int64_t offset;   // Posición en bytes en el archivo de datos
};

class BST {

private:
    // Cada nodo del árbol
    struct Node {
        std::string key;     // Valor de la columna indexada (guardado como string)
        int64_t     offset;  // Posición en el archivo donde está el registro completo
        Node*       left;
        Node*       right;

        Node(const std::string& k, int64_t off)
            : key(k), offset(off), left(nullptr), right(nullptr) {}
    };

    Node* root;         // Raíz del árbol
    int   nodeCount;    // Cuántos nodos tiene el árbol

    // ----------------------------------------------------------------
    // Helpers privados (recursivos)
    // ----------------------------------------------------------------

    // Inserta un nodo nuevo. Retorna false si la clave ya existe (no se permiten duplicados).
    bool insertRec(Node*& node, const std::string& key, int64_t offset);

    // Busca una clave exacta. Retorna el nodo o nullptr.
    Node* searchRec(Node* node, const std::string& key) const;

    // Elimina un nodo con la clave dada.
    Node* deleteRec(Node* node, const std::string& key, bool& deleted);

    // Encuentra el nodo con la clave más pequeña del subárbol (para el delete).
    Node* findMin(Node* node) const;

    // Destruye todos los nodos (usado en el destructor).
    void destroyRec(Node* node);

    // Recorre el árbol en orden (izquierda → raíz → derecha) y llama callback en cada nodo.
    // Esto da los elementos ordenados de menor a mayor.
    void inOrderRec(Node* node, std::vector<std::pair<std::string, int64_t>>& out) const;

    // Recorre buscando todos los offsets cuya clave cumpla una condición
    void rangeSearchRec(Node* node,
                        const std::string& value,
                        const std::string& op,
                        std::vector<int64_t>& results) const;

    // Compara dos claves. Retorna -1, 0 o 1 (como strcmp pero para nuestros tipos).
    int compareKeys(const std::string& a, const std::string& b) const;

public:
    BST();
    ~BST();

    // ----------------------------------------------------------------
    // Operaciones principales
    // ----------------------------------------------------------------

    // Inserta un par (clave, offset).
    // Retorna false si la clave ya existe (el índice no permite duplicados).
    bool insert(const std::string& key, int64_t offset);

    // Busca la clave exacta. Retorna el offset si existe.
    BSTSearchResult search(const std::string& key) const;

    // Elimina el nodo con esa clave.
    // Retorna true si se eliminó, false si no existía.
    bool remove(const std::string& key);

    // Actualiza el offset de una clave que ya existe.
    // Útil cuando se modifica un registro en disco y cambia de posición.
    bool updateOffset(const std::string& key, int64_t newOffset);

    // ----------------------------------------------------------------
    // Búsquedas con operadores (para el WHERE del SELECT)
    // ----------------------------------------------------------------

    // Retorna los offsets de todos los registros donde la clave cumple la condición.
    // op puede ser: "=", ">", "<", ">=", "<=", "!="
    std::vector<int64_t> searchByOperator(const std::string& value, const std::string& op) const;

    // ----------------------------------------------------------------
    // Utilidades
    // ----------------------------------------------------------------

    // Retorna todos los pares (clave, offset) en orden ascendente de clave.
    // Útil para reconstruir el árbol al reiniciar el servidor.
    std::vector<std::pair<std::string, int64_t>> getAllOrdered() const;

    // ¿Existe ya esta clave en el árbol?
    bool contains(const std::string& key) const;

    // ¿Cuántos nodos tiene el árbol?
    int size() const;

    // ¿Está vacío?
    bool isEmpty() const;

    // Vacía el árbol completamente (por ejemplo al hacer DROP TABLE)
    void clear();
};

#endif // TINYSQLDB_BST_H