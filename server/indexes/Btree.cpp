//
// BTree.cpp
//

#include "BTree.h"
#include <algorithm>
#include <stdexcept>

// ============================================================
// Constructor y Destructor
// ============================================================

BTree::BTree(int t) : root(nullptr), T(t), nodeCount(0) {
    // La raíz empieza vacía como hoja
    root = new Node(true);
}

BTree::~BTree() {
    delete root;  // El destructor de Node borra recursivamente todos los hijos
}

void BTree::clear() {
    delete root;
    root      = new Node(true);
    nodeCount = 0;
}

// ============================================================
// Comparación de claves
// ============================================================

int BTree::compareKeys(const std::string& a, const std::string& b) const {
    // Si ambas son enteros, comparar como número
    bool aIsInt = !a.empty() && std::all_of(a.begin(), a.end(), ::isdigit);
    bool bIsInt = !b.empty() && std::all_of(b.begin(), b.end(), ::isdigit);

    if (aIsInt && bIsInt) {
        long long numA = std::stoll(a);
        long long numB = std::stoll(b);
        if (numA < numB) return -1;
        if (numA > numB) return  1;
        return 0;
    }

    // Intentar como double
    try {
        double dA = std::stod(a);
        double dB = std::stod(b);
        if (dA < dB) return -1;
        if (dA > dB) return  1;
        return 0;
    } catch (...) {}

    // Comparación lexicográfica
    if (a < b) return -1;
    if (a > b) return  1;
    return 0;
}

// ============================================================
// SEARCH
// ============================================================

BTreeSearchResult BTree::search(const std::string& key) const {
    return searchRec(root, key);
}

BTreeSearchResult BTree::searchRec(Node* node, const std::string& key) const {
    // Encontrar la primera entrada cuya clave >= key
    int i = 0;
    while (i < (int)node->entries.size() && compareKeys(key, node->entries[i].key) > 0) {
        i++;
    }

    // ¿Encontramos la clave exactamente?
    if (i < (int)node->entries.size() && compareKeys(key, node->entries[i].key) == 0) {
        return {true, node->entries[i].offset};
    }

    // Si es hoja y no encontramos, la clave no existe
    if (node->isLeaf) {
        return {false, -1};
    }

    // Bajar al hijo correspondiente
    return searchRec(node->children[i], key);
}

bool BTree::contains(const std::string& key) const {
    return containsRec(root, key);
}

bool BTree::containsRec(Node* node, const std::string& key) const {
    return searchRec(node, key).found;
}

// ============================================================
// INSERT
// ============================================================

bool BTree::insert(const std::string& key, int64_t offset) {
    // Verificar duplicados antes de insertar
    if (contains(key)) return false;

    // Si la raíz está llena, hay que dividirla
    if ((int)root->entries.size() == 2 * T - 1) {
        Node* newRoot = new Node(false);  // Nueva raíz no es hoja
        newRoot->children.push_back(root);
        splitChild(newRoot, 0, root);
        root = newRoot;
    }

    insertNonFull(root, key, offset);
    nodeCount++;
    return true;
}

// Inserta en un nodo que garantizamos NO está lleno
bool BTree::insertNonFull(Node* node, const std::string& key, int64_t offset) {
    int i = (int)node->entries.size() - 1;

    if (node->isLeaf) {
        // Insertar en la posición correcta dentro de las entradas del nodo
        // Hacemos espacio desplazando las entradas mayores hacia la derecha
        node->entries.push_back(BTreeEntry("", 0));  // Espacio temporal
        while (i >= 0 && compareKeys(key, node->entries[i].key) < 0) {
            node->entries[i + 1] = node->entries[i];
            i--;
        }
        node->entries[i + 1] = BTreeEntry(key, offset);
    } else {
        // Encontrar el hijo donde debe ir la clave
        while (i >= 0 && compareKeys(key, node->entries[i].key) < 0) {
            i--;
        }
        i++;  // El hijo está en la posición i

        // Si ese hijo está lleno, dividirlo primero
        if ((int)node->children[i]->entries.size() == 2 * T - 1) {
            splitChild(node, i, node->children[i]);

            // Después del split, la clave del medio subió a 'node'.
            // Decidir a cuál de los dos nuevos hijos bajar.
            if (compareKeys(key, node->entries[i].key) > 0) {
                i++;
            }
        }
        insertNonFull(node->children[i], key, offset);
    }
    return true;
}

// Divide el hijo 'child' (que está lleno) del nodo 'parent'
// en la posición 'childIndex'.
// Después del split:
//   - La mitad izquierda se queda en 'child'
//   - La clave del medio sube a 'parent'
//   - La mitad derecha se convierte en un nuevo hijo de 'parent'
void BTree::splitChild(Node* parent, int childIndex, Node* child) {
    Node* newNode = new Node(child->isLeaf);

    // La clave del medio que sube al padre
    BTreeEntry midEntry = child->entries[T - 1];

    // Las claves de la mitad derecha van al nuevo nodo
    for (int j = T; j < 2 * T - 1; j++) {
        newNode->entries.push_back(child->entries[j]);
    }

    // Si no es hoja, los hijos de la mitad derecha también se mueven
    if (!child->isLeaf) {
        for (int j = T; j < 2 * T; j++) {
            newNode->children.push_back(child->children[j]);
        }
        child->children.resize(T);
    }

    // El hijo original se queda solo con la mitad izquierda
    child->entries.resize(T - 1);

    // Insertar el nuevo nodo como hijo del padre
    parent->children.insert(parent->children.begin() + childIndex + 1, newNode);

    // Insertar la clave del medio en el padre
    parent->entries.insert(parent->entries.begin() + childIndex, midEntry);
}

// ============================================================
// DELETE
// ============================================================

bool BTree::remove(const std::string& key) {
    if (!contains(key)) return false;

    deleteRec(root, key);
    nodeCount--;

    // Si la raíz queda vacía y tiene un hijo, ese hijo se convierte en la nueva raíz
    if (root->entries.empty() && !root->isLeaf) {
        Node* oldRoot = root;
        root = root->children[0];
        oldRoot->children.clear();  // Evitar que el destructor borre el nuevo root
        delete oldRoot;
    }

    return true;
}

bool BTree::deleteRec(Node* node, const std::string& key) {
    int idx = 0;
    while (idx < (int)node->entries.size() && compareKeys(key, node->entries[idx].key) > 0) {
        idx++;
    }

    bool keyInThisNode = (idx < (int)node->entries.size() &&
                          compareKeys(key, node->entries[idx].key) == 0);

    if (keyInThisNode) {
        if (node->isLeaf) {
            // Caso 1: La clave está en una hoja — simplemente eliminar
            node->entries.erase(node->entries.begin() + idx);
        } else {
            // Caso 2: La clave está en un nodo interno
            Node* leftChild  = node->children[idx];
            Node* rightChild = node->children[idx + 1];

            if ((int)leftChild->entries.size() >= T) {
                // Caso 2a: El hijo izquierdo tiene suficientes claves
                // Reemplazar con el predecesor
                BTreeEntry pred = getPredecessor(node, idx);
                node->entries[idx] = pred;
                deleteRec(leftChild, pred.key);

            } else if ((int)rightChild->entries.size() >= T) {
                // Caso 2b: El hijo derecho tiene suficientes claves
                // Reemplazar con el sucesor
                BTreeEntry succ = getSuccessor(node, idx);
                node->entries[idx] = succ;
                deleteRec(rightChild, succ.key);

            } else {
                // Caso 2c: Ambos hijos tienen T-1 claves → fusionar
                merge(node, idx);
                deleteRec(leftChild, key);
            }
        }
    } else {
        // La clave no está en este nodo
        if (node->isLeaf) return false;  // No existe

        // Asegurar que el hijo al que vamos tiene al menos T claves
        bool isLastChild = (idx == (int)node->entries.size());
        if ((int)node->children[idx]->entries.size() < T) {
            fill(node, idx);
        }

        // Después de fill, el nodo puede haber cambiado de índice si se fusionó
        if (isLastChild && idx > (int)node->entries.size()) {
            deleteRec(node->children[idx - 1], key);
        } else {
            deleteRec(node->children[idx], key);
        }
    }
    return true;
}

BTreeEntry BTree::getPredecessor(Node* node, int idx) {
    // El predecesor es la clave más grande del subárbol izquierdo
    Node* cur = node->children[idx];
    while (!cur->isLeaf) {
        cur = cur->children.back();
    }
    return cur->entries.back();
}

BTreeEntry BTree::getSuccessor(Node* node, int idx) {
    // El sucesor es la clave más pequeña del subárbol derecho
    Node* cur = node->children[idx + 1];
    while (!cur->isLeaf) {
        cur = cur->children.front();
    }
    return cur->entries.front();
}

// Asegura que el hijo en posición idx tenga al menos T claves
void BTree::fill(Node* node, int idx) {
    if (idx > 0 && (int)node->children[idx - 1]->entries.size() >= T) {
        borrowFromPrev(node, idx);
    } else if (idx < (int)node->entries.size() && (int)node->children[idx + 1]->entries.size() >= T) {
        borrowFromNext(node, idx);
    } else {
        // Fusionar con un hermano
        if (idx < (int)node->entries.size()) {
            merge(node, idx);
        } else {
            merge(node, idx - 1);
        }
    }
}

void BTree::borrowFromPrev(Node* node, int idx) {
    Node* child  = node->children[idx];
    Node* sibling = node->children[idx - 1];

    // Desplazar todas las entradas del hijo una posición a la derecha
    child->entries.insert(child->entries.begin(), node->entries[idx - 1]);

    // Si no son hojas, mover el último hijo del hermano al hijo
    if (!child->isLeaf) {
        child->children.insert(child->children.begin(), sibling->children.back());
        sibling->children.pop_back();
    }

    // La última clave del hermano sube al padre
    node->entries[idx - 1] = sibling->entries.back();
    sibling->entries.pop_back();
}

void BTree::borrowFromNext(Node* node, int idx) {
    Node* child   = node->children[idx];
    Node* sibling = node->children[idx + 1];

    // La clave del padre baja al final del hijo
    child->entries.push_back(node->entries[idx]);

    // Si no son hojas, mover el primer hijo del hermano al hijo
    if (!child->isLeaf) {
        child->children.push_back(sibling->children.front());
        sibling->children.erase(sibling->children.begin());
    }

    // La primera clave del hermano sube al padre
    node->entries[idx] = sibling->entries.front();
    sibling->entries.erase(sibling->entries.begin());
}

// Fusiona el hijo en idx con el hijo en idx+1
void BTree::merge(Node* node, int idx) {
    Node* leftChild  = node->children[idx];
    Node* rightChild = node->children[idx + 1];

    // La clave del padre baja al hijo izquierdo
    leftChild->entries.push_back(node->entries[idx]);

    // Mover todas las entradas del hijo derecho al izquierdo
    for (auto& entry : rightChild->entries) {
        leftChild->entries.push_back(entry);
    }

    // Mover los hijos del hijo derecho al izquierdo (si no son hojas)
    if (!leftChild->isLeaf) {
        for (Node* child : rightChild->children) {
            leftChild->children.push_back(child);
        }
        rightChild->children.clear();  // Evitar doble borrado
    }

    // Eliminar la clave del padre y el puntero al hijo derecho
    node->entries.erase(node->entries.begin() + idx);
    node->children.erase(node->children.begin() + idx + 1);

    rightChild->entries.clear();
    delete rightChild;
}

// ============================================================
// UPDATE OFFSET
// ============================================================

bool BTree::updateOffset(const std::string& key, int64_t newOffset) {
    // Buscar el nodo que contiene la clave y actualizar su offset
    // Usamos una función auxiliar inline ya que necesitamos modificar el nodo
    std::function<bool(Node*)> updateRec = [&](Node* node) -> bool {
        int i = 0;
        while (i < (int)node->entries.size() && compareKeys(key, node->entries[i].key) > 0) {
            i++;
        }
        if (i < (int)node->entries.size() && compareKeys(key, node->entries[i].key) == 0) {
            node->entries[i].offset = newOffset;
            return true;
        }
        if (node->isLeaf) return false;
        return updateRec(node->children[i]);
    };
    return updateRec(root);
}

// ============================================================
// BÚSQUEDA CON OPERADORES (para el WHERE)
// ============================================================

std::vector<int64_t> BTree::searchByOperator(const std::string& value,
                                               const std::string& op) const {
    std::vector<int64_t> results;
    rangeSearchRec(root, value, op, results);
    return results;
}

void BTree::rangeSearchRec(Node* node,
                            const std::string& value,
                            const std::string& op,
                            std::vector<int64_t>& results) const {
    int i = 0;
    while (i < (int)node->entries.size()) {
        // Primero bajar al hijo izquierdo (si no es hoja)
        if (!node->isLeaf) {
            // Para "<" y "<=", solo vale la pena bajar si la clave del nodo >= value
            if (op == "<" || op == "<=") {
                if (compareKeys(node->entries[i].key, value) >= 0) {
                    rangeSearchRec(node->children[i], value, op, results);
                } else {
                    rangeSearchRec(node->children[i], value, op, results);
                }
            } else {
                rangeSearchRec(node->children[i], value, op, results);
            }
        }

        // Evaluar la clave actual
        int cmp = compareKeys(node->entries[i].key, value);
        bool matches = false;

        if      (op == "=")   matches = (cmp == 0);
        else if (op == ">")   matches = (cmp > 0);
        else if (op == "<")   matches = (cmp < 0);
        else if (op == ">=")  matches = (cmp >= 0);
        else if (op == "<=")  matches = (cmp <= 0);
        else if (op == "!=" || op == "not") matches = (cmp != 0);

        if (matches) {
            results.push_back(node->entries[i].offset);
        }

        // Para "=" podemos parar si ya la clave es mayor al valor buscado
        if (op == "=" && cmp > 0) break;

        i++;
    }

    // Bajar al último hijo (si no es hoja)
    if (!node->isLeaf) {
        rangeSearchRec(node->children[i], value, op, results);
    }
}

// ============================================================
// UTILIDADES
// ============================================================

std::vector<std::pair<std::string, int64_t>> BTree::getAllOrdered() const {
    std::vector<std::pair<std::string, int64_t>> result;
    inOrderRec(root, result);
    return result;
}

void BTree::inOrderRec(Node* node,
                        std::vector<std::pair<std::string, int64_t>>& out) const {
    if (node == nullptr) return;

    for (int i = 0; i < (int)node->entries.size(); i++) {
        // Bajar al hijo izquierdo antes de agregar esta clave
        if (!node->isLeaf) {
            inOrderRec(node->children[i], out);
        }
        out.push_back({node->entries[i].key, node->entries[i].offset});
    }

    // Bajar al último hijo
    if (!node->isLeaf) {
        inOrderRec(node->children.back(), out);
    }
}

int BTree::size() const {
    return nodeCount;
}

bool BTree::isEmpty() const {
    return nodeCount == 0;
}