import { useState, useRef, useEffect } from "react";

const API_URL = "http://localhost:8080/query";

export default function SqlEditor({ activeDatabase, onDatabaseChange }) {
  const [sql, setSql] = useState("");
  const [result, setResult] = useState(null);
  const [loading, setLoading] = useState(false);
  const [history, setHistory] = useState([]);
  const [historyIndex, setHistoryIndex] = useState(-1);
  const textareaRef = useRef(null);

  // Navegar historial con flechas
  const handleKeyDown = (e) => {
    if (e.key === "ArrowUp") {
      e.preventDefault();
      const next = Math.min(historyIndex + 1, history.length - 1);
      setHistoryIndex(next);
      if (history[next] !== undefined) setSql(history[next]);
    } else if (e.key === "ArrowDown") {
      e.preventDefault();
      const next = Math.max(historyIndex - 1, -1);
      setHistoryIndex(next);
      setSql(next === -1 ? "" : history[next]);
    } else if ((e.ctrlKey || e.metaKey) && e.key === "Enter") {
      e.preventDefault();
      handleExecute();
    }
  };

  const handleExecute = async () => {
    const query = sql.trim();
    if (!query) return;

    setLoading(true);
    setResult(null);

    // Guardar en historial
    setHistory((prev) => [query, ...prev.slice(0, 49)]);
    setHistoryIndex(-1);

    // Detectar SET DATABASE localmente también
    const setDbMatch = query.match(/^\s*SET\s+DATABASE\s+(\w+)\s*;?\s*$/i);

    try {
      const body = { sql: query };
      if (activeDatabase) body.database = activeDatabase;

      const response = await fetch(API_URL, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(body),
      });

      const data = await response.json();

      // Si fue SET DATABASE exitoso, actualizar contexto
      if (setDbMatch && data.status === "ok") {
        onDatabaseChange(setDbMatch[1]);
      }

      setResult(data);
    } catch (err) {
      setResult({
        status: "error",
        message: "No se pudo conectar al servidor. Verifique que el backend está corriendo.",
        time_ms: 0,
      });
    } finally {
      setLoading(false);
    }
  };

  return (
    <div className="editor-wrapper">
      {/* Barra de estado de base de datos */}
      <div className="db-bar">
        <span className="db-label">Base de datos activa:</span>
        {activeDatabase ? (
          <span className="db-name">{activeDatabase}</span>
        ) : (
          <span className="db-none">ninguna — usa SET DATABASE nombre;</span>
        )}
      </div>

      {/* Área de texto SQL */}
      <div className="textarea-container">
        <textarea
          ref={textareaRef}
          className="sql-textarea"
          value={sql}
          onChange={(e) => setSql(e.target.value)}
          onKeyDown={handleKeyDown}
          placeholder={"SELECT * FROM tabla;\nINSERT INTO tabla VALUES (...);\n\n-- Ctrl+Enter para ejecutar\n-- ↑↓ para navegar historial"}
          spellCheck={false}
          autoComplete="off"
          autoCorrect="off"
        />
      </div>

      {/* Botones */}
      <div className="toolbar">
        <button
          className="btn-execute"
          onClick={handleExecute}
          disabled={loading || !sql.trim()}
        >
          {loading ? (
            <>
              <span className="spinner" /> Ejecutando...
            </>
          ) : (
            <>▶ Ejecutar</>
          )}
        </button>

        <button
          className="btn-clear"
          onClick={() => { setSql(""); setResult(null); }}
          disabled={loading}
        >
          Limpiar
        </button>

        {result?.time_ms !== undefined && (
          <span className="exec-time">⏱ {result.time_ms} ms</span>
        )}
      </div>

      {/* Resultado */}
      {result && <QueryResult result={result} />}
    </div>
  );
}

function QueryResult({ result }) {
  if (result.status === "error") {
    return (
      <div className="result-error">
        <span className="result-icon">✖</span>
        <span>{result.message}</span>
      </div>
    );
  }

  // Respuesta sin filas (DDL, INSERT, UPDATE, DELETE, SET DATABASE)
  if (!result.columns || result.columns.length === 0) {
    return (
      <div className="result-success">
        <span className="result-icon">✔</span>
        <span>{result.message || "Consulta ejecutada correctamente."}</span>
      </div>
    );
  }

  // SELECT con datos
  return (
    <div className="result-table-wrapper">
      <div className="result-meta">
        {result.rows?.length ?? 0} filas · {result.columns.length} columnas
      </div>
      <div className="table-scroll">
        <table className="result-table">
          <thead>
            <tr>
              {result.columns.map((col) => (
                <th key={col}>{col}</th>
              ))}
            </tr>
          </thead>
          <tbody>
            {result.rows && result.rows.length > 0 ? (
              result.rows.map((row, i) => (
                <tr key={i}>
                  {row.map((cell, j) => (
                    <td key={j}>{cell}</td>
                  ))}
                </tr>
              ))
            ) : (
              <tr>
                <td colSpan={result.columns.length} className="empty-row">
                  Sin resultados
                </td>
              </tr>
            )}
          </tbody>
        </table>
      </div>
    </div>
  );
}