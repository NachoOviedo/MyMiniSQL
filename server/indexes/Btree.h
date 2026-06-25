//
// BTree.h
// Árbol B para índices de TinySQLDb
//
// ¿Por qué un BTree y no solo un BST?
//   El BTree es más ancho y menos profundo que el BST.
//   Con millones de registros, un BST puede tener miles de niveles.
//   Un BTree de orden 100 raramente pasa de 4 niveles para la misma cantidad.
//   Menos niveles = menos comparaciones = búsquedas más rápidas.
//
// Orden del árbol (T):
//   - Cada nodo interno tiene entre T y 2T hijos (excepto la raíz)
//   - Cada nodo guarda entre T-1 y 2T-1 claves
//   - Usamos T=3 por defecto (orden mínimo razonable para demostrar splits)
//
// Cada clave en un nodo guarda:
//   - El valor de la columna indexada (string)
//   - El offset en disco donde está el registro completo (int64_t)

#ifndef TINYSQLDB_BTREE_H
#define TINYSQLDB_BTREE_H

#include <string>
#include <vector>
#include <cstdint>

// Resultado de una búsqueda en el BTree
struct BTreeSearchResult {
    bool    found;
    int64_t offset;
};

// Una entrada dentro de un nodo: valor de columna + posición en disco
struct BTreeEntry {
    std::string key;
    int64_t     offset;

    BTreeEntry() : key(""), offset(0) {}
    BTreeEntry(const std::string& k, int64_t off) : key(k), offset(off) {}
};

class BTree {
private:
    // ----------------------------------------------------------------
    // Nodo del BTree
    // ----------------------------------------------------------------
    struct Node {
        std::vector<BTreeEntry> entries;   // Claves + offsets de este nodo
        std::vector<Node*>      children;  // Punteros a hijos
        bool                    isLeaf;    // ¿Es hoja? (no tiene hijos)

        Node(bool leaf) : isLeaf(leaf) {}

        ~Node() {
            for (Node* child : children) {
                delete child;
            }
        }
    };

    Node* root;       // Raíz del árbol
    int   T;          // Orden mínimo (cada nodo tiene entre T-1 y 2T-1 claves)
    int   nodeCount;  // Cuántos nodos (entradas) hay en total

    // ----------------------------------------------------------------
    // Helpers privados
    // ----------------------------------------------------------------

    // Compara dos claves. Retorna -1, 0 o 1.
    // Si ambas son numéricas, compara como número (para que "10" > "9")
    int compareKeys(const std::string& a, const std::string& b) const;

    // Busca la clave en el subárbol con raíz en 'node'
    BTreeSearchResult searchRec(Node* node, const std::string& key) const;

    // Inserta en un nodo que NO está lleno
    // Precondición: node no tiene 2T-1 claves
    bool insertNonFull(Node* node, const std::string& key, int64_t offset);

    // Divide el hijo 'child' del nodo 'parent' en la posición 'childIndex'
    // El hijo debe estar lleno (2T-1 claves) antes de llamar esto
    void splitChild(Node* parent, int childIndex, Node* child);

    // Elimina la clave del subárbol con raíz en 'node'
    bool deleteRec(Node* node, const std::string& key);

    // Obtiene el predecesor de la clave en posición 'idx' dentro de 'node'
    BTreeEntry getPredecessor(Node* node, int idx);

    // Obtiene el sucesor de la clave en posición 'idx' dentro de 'node'
    BTreeEntry getSuccessor(Node* node, int idx);

    // Asegura que el hijo en posición 'idx' tenga al menos T claves
    void fill(Node* node, int idx);

    // Toma prestada una clave del hermano izquierdo
    void borrowFromPrev(Node* node, int idx);

    // Toma prestada una clave del hermano derecho
    void borrowFromNext(Node* node, int idx);

    // Fusiona el hijo en posición 'idx' con el hijo en posición 'idx+1'
    void merge(Node* node, int idx);

    // Recorre el árbol buscando entradas que cumplan la condición (para WHERE)
    void rangeSearchRec(Node* node,
                        const std::string& value,
                        const std::string& op,
                        std::vector<int64_t>& results) const;

    // Recorre en orden y agrega todos los pares (key, offset) a 'out'
    void inOrderRec(Node* node,
                    std::vector<std::pair<std::string, int64_t>>& out) const;

    // Verifica si una clave ya existe (para validar duplicados)
    bool containsRec(Node* node, const std::string& key) const;

public:
    // t: orden mínimo del árbol (default 3)
    BTree(int t = 3);
    ~BTree();

    // ----------------------------------------------------------------
    // Operaciones principales
    // ----------------------------------------------------------------

    // Inserta un par (clave, offset en disco).
    // Retorna false si la clave ya existe (no se permiten duplicados en índices).
    bool insert(const std::string& key, int64_t offset);

    // Busca la clave exacta. Retorna el offset si existe.
    BTreeSearchResult search(const std::string& key) const;

    // Elimina el nodo con esa clave.
    // Retorna true si se eliminó, false si no existía.
    bool remove(const std::string& key);

    // Actualiza el offset de una clave existente (cuando el registro cambia de posición).
    bool updateOffset(const std::string& key, int64_t newOffset);

    // ----------------------------------------------------------------
    // Búsquedas con operadores (para el WHERE del SELECT)
    // ----------------------------------------------------------------

    // Retorna offsets de registros donde la clave cumple la condición.
    // op puede ser: "=", ">", "<", ">=", "<=", "!="
    std::vector<int64_t> searchByOperator(const std::string& value,
                                          const std::string& op) const;

    // ----------------------------------------------------------------
    // Utilidades
    // ----------------------------------------------------------------

    // ¿Existe ya esta clave?
    bool contains(const std::string& key) const;

    // Retorna todos los pares (clave, offset) en orden ascendente.
    // Útil para reconstruir el árbol al reiniciar el servidor.
    std::vector<std::pair<std::string, int64_t>> getAllOrdered() const;

    int  size()    const;
    bool isEmpty() const;
    void clear();
};

#endif // TINYSQLDB_BTREE_H