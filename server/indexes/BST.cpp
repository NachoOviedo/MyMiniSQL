//
// BST.cpp
//

#include "BST.h"
#include <stdexcept>
#include <algorithm>

// ============================================================
// Constructor y Destructor
// ============================================================

BST::BST() : root(nullptr), nodeCount(0) {}

BST::~BST() {
    destroyRec(root);
}

// Destruye todos los nodos en post-orden (hijos primero, luego padre)
void BST::destroyRec(Node* node) {
    if (node == nullptr) return;
    destroyRec(node->left);
    destroyRec(node->right);
    delete node;
}

// ============================================================
// Comparación de claves
// ============================================================

// Compara dos claves como strings.
// Si ambas son numéricas puras, las compara como números para que
// "10" > "9" (en vez de comparar letra por letra).
int BST::compareKeys(const std::string& a, const std::string& b) const {
    // Intentar comparar como números si ambas son numéricas
    bool aIsNum = !a.empty() && (std::all_of(a.begin(), a.end(), ::isdigit) ||
                  (a[0] == '-' && a.size() > 1 && std::all_of(a.begin()+1, a.end(), ::isdigit)));
    bool bIsNum = !b.empty() && (std::all_of(b.begin(), b.end(), ::isdigit) ||
                  (b[0] == '-' && b.size() > 1 && std::all_of(b.begin()+1, b.end(), ::isdigit)));

    if (aIsNum && bIsNum) {
        long long numA = std::stoll(a);
        long long numB = std::stoll(b);
        if (numA < numB) return -1;
        if (numA > numB) return  1;
        return 0;
    }

    // Si tienen punto decimal, comparar como double
    try {
        double dA = std::stod(a);
        double dB = std::stod(b);
        if (dA < dB) return -1;
        if (dA > dB) return  1;
        return 0;
    } catch (...) {}

    // Comparación lexicográfica normal
    if (a < b) return -1;
    if (a > b) return  1;
    return 0;
}

// ============================================================
// INSERT
// ============================================================

bool BST::insert(const std::string& key, int64_t offset) {
    bool inserted = insertRec(root, key, offset);
    if (inserted) nodeCount++;
    return inserted;
}

// Versión recursiva del insert.
// node es una referencia al puntero, así podemos modificarlo directamente.
bool BST::insertRec(Node*& node, const std::string& key, int64_t offset) {
    if (node == nullptr) {
        // Llegamos a un lugar vacío: crear el nodo aquí
        node = new Node(key, offset);
        return true;
    }

    int cmp = compareKeys(key, node->key);

    if (cmp < 0) {
        // La clave es menor: ir a la izquierda
        return insertRec(node->left, key, offset);
    } else if (cmp > 0) {
        // La clave es mayor: ir a la derecha
        return insertRec(node->right, key, offset);
    } else {
        // La clave ya existe — los índices no permiten duplicados
        return false;
    }
}

// ============================================================
// SEARCH (búsqueda exacta)
// ============================================================

BSTSearchResult BST::search(const std::string& key) const {
    Node* found = searchRec(root, key);
    if (found == nullptr) {
        return {false, -1};
    }
    return {true, found->offset};
}

BST::Node* BST::searchRec(Node* node, const std::string& key) const {
    if (node == nullptr) return nullptr;  // No existe

    int cmp = compareKeys(key, node->key);

    if (cmp == 0) return node;           // ¡Encontrado!
    if (cmp < 0)  return searchRec(node->left,  key);
    else           return searchRec(node->right, key);
}

bool BST::contains(const std::string& key) const {
    return searchRec(root, key) != nullptr;
}

// ============================================================
// DELETE
// ============================================================

bool BST::remove(const std::string& key) {
    bool deleted = false;
    root = deleteRec(root, key, deleted);
    if (deleted) nodeCount--;
    return deleted;
}

// Eliminar un nodo de un BST tiene 3 casos:
//   1. El nodo es hoja (no tiene hijos): simplemente se borra.
//   2. El nodo tiene un solo hijo: se reemplaza por ese hijo.
//   3. El nodo tiene dos hijos: se reemplaza por el sucesor
//      (el nodo más pequeño del subárbol derecho).
BST::Node* BST::deleteRec(Node* node, const std::string& key, bool& deleted) {
    if (node == nullptr) {
        deleted = false;
        return nullptr;
    }

    int cmp = compareKeys(key, node->key);

    if (cmp < 0) {
        node->left = deleteRec(node->left, key, deleted);
    } else if (cmp > 0) {
        node->right = deleteRec(node->right, key, deleted);
    } else {
        // Este es el nodo a borrar
        deleted = true;

        if (node->left == nullptr && node->right == nullptr) {
            // Caso 1: hoja
            delete node;
            return nullptr;

        } else if (node->left == nullptr) {
            // Caso 2: solo tiene hijo derecho
            Node* temp = node->right;
            delete node;
            return temp;

        } else if (node->right == nullptr) {
            // Caso 2: solo tiene hijo izquierdo
            Node* temp = node->left;
            delete node;
            return temp;

        } else {
            // Caso 3: tiene dos hijos
            // Buscar el sucesor (mínimo del subárbol derecho)
            Node* successor = findMin(node->right);
            // Copiar los datos del sucesor a este nodo
            node->key    = successor->key;
            node->offset = successor->offset;
            // Eliminar el sucesor del subárbol derecho
            bool dummy = false;
            node->right = deleteRec(node->right, successor->key, dummy);
        }
    }
    return node;
}

BST::Node* BST::findMin(Node* node) const {
    while (node->left != nullptr) {
        node = node->left;
    }
    return node;
}

// ============================================================
// UPDATE OFFSET
// ============================================================

// Cuando un registro cambia de posición en el archivo (por ejemplo tras
// una compactación), actualizamos el offset sin mover el nodo en el árbol.
bool BST::updateOffset(const std::string& key, int64_t newOffset) {
    Node* node = searchRec(root, key);
    if (node == nullptr) return false;
    node->offset = newOffset;
    return true;
}

// ============================================================
// BÚSQUEDA CON OPERADORES (para el WHERE)
// ============================================================

std::vector<int64_t> BST::searchByOperator(const std::string& value, const std::string& op) const {
    std::vector<int64_t> results;
    rangeSearchRec(root, value, op, results);
    return results;
}

// Recorre TODO el árbol y filtra los nodos que cumplan la condición.
// Para "=" usamos el search rápido en O(log n).
// Para los demás operadores hacemos un recorrido en orden que
// permite podar ramas enteras (por ejemplo, si buscamos > 5 y
// estamos en un nodo con clave 3, solo vamos a la derecha).
void BST::rangeSearchRec(Node* node,
                          const std::string& value,
                          const std::string& op,
                          std::vector<int64_t>& results) const {
    if (node == nullptr) return;

    int cmp = compareKeys(node->key, value);

    if (op == "=") {
        // Búsqueda exacta: podemos podar ramas
        if (cmp == 0) {
            results.push_back(node->offset);
        } else if (cmp < 0) {
            // Este nodo es menor, el valor podría estar a la derecha
            rangeSearchRec(node->right, value, op, results);
        } else {
            // Este nodo es mayor, el valor podría estar a la izquierda
            rangeSearchRec(node->left, value, op, results);
        }

    } else if (op == ">") {
        // Queremos claves mayores al valor buscado
        if (cmp > 0) {
            // Este nodo califica; también buscar en ambos subárboles
            results.push_back(node->offset);
            rangeSearchRec(node->left,  value, op, results);
            rangeSearchRec(node->right, value, op, results);
        } else {
            // Este nodo no califica; solo la derecha puede tener mayores
            rangeSearchRec(node->right, value, op, results);
        }

    } else if (op == "<") {
        if (cmp < 0) {
            results.push_back(node->offset);
            rangeSearchRec(node->left,  value, op, results);
            rangeSearchRec(node->right, value, op, results);
        } else {
            rangeSearchRec(node->left, value, op, results);
        }

    } else if (op == ">=") {
        if (cmp >= 0) {
            results.push_back(node->offset);
            rangeSearchRec(node->left,  value, op, results);
            rangeSearchRec(node->right, value, op, results);
        } else {
            rangeSearchRec(node->right, value, op, results);
        }

    } else if (op == "<=") {
        if (cmp <= 0) {
            results.push_back(node->offset);
            rangeSearchRec(node->left,  value, op, results);
            rangeSearchRec(node->right, value, op, results);
        } else {
            rangeSearchRec(node->left, value, op, results);
        }

    } else if (op == "!=" || op == "not") {
        // Todos excepto el que tenga esa clave exacta
        if (cmp != 0) results.push_back(node->offset);
        rangeSearchRec(node->left,  value, op, results);
        rangeSearchRec(node->right, value, op, results);
    }
}

// ============================================================
// UTILIDADES
// ============================================================

std::vector<std::pair<std::string, int64_t>> BST::getAllOrdered() const {
    std::vector<std::pair<std::string, int64_t>> result;
    inOrderRec(root, result);
    return result;
}

// Recorrido en orden: izquierda → nodo → derecha
// Esto produce los elementos ordenados de menor a mayor
void BST::inOrderRec(Node* node, std::vector<std::pair<std::string, int64_t>>& out) const {
    if (node == nullptr) return;
    inOrderRec(node->left, out);
    out.push_back({node->key, node->offset});
    inOrderRec(node->right, out);
}

int BST::size() const {
    return nodeCount;
}

bool BST::isEmpty() const {
    return nodeCount == 0;
}

void BST::clear() {
    destroyRec(root);
    root      = nullptr;
    nodeCount = 0;
}