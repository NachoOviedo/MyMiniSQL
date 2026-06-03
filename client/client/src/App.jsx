import { useState } from "react";
import SqlEditor from "./SqlEditor";
import "./App.css";

export default function App() {
  const [activeDatabase, setActiveDatabase] = useState("");

  return (
    <div className="app">
      <header className="app-header">
        <h1 className="app-title">TinySQLDB</h1>
        <p className="app-subtitle">Motor de base de datos relacional</p>
      </header>

      <main className="app-main">
        <SqlEditor
          activeDatabase={activeDatabase}
          onDatabaseChange={setActiveDatabase}
        />
      </main>
    </div>
  );
}

/* cd client
npm install
npm run dev */