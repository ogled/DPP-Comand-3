import { useEffect, useState } from "react";
import { startPolling, usePersistent } from "./store.js";
import TopBar from "./components/TopBar.jsx";
import Connection from "./components/Connection.jsx";
import MccView from "./components/MccView.jsx";
import MonitorView from "./components/MonitorView.jsx";
import ProtocolEditor from "./components/ProtocolEditor.jsx";
import Toasts from "./components/Toasts.jsx";

export default function App() {
  const [mode, setMode] = usePersistent("dpp.mode", "mcc");
  const [protoOpen, setProtoOpen] = useState(false);

  useEffect(() => startPolling(), []);

  return (
    <div className="app" data-mode={mode}>
      <TopBar mode={mode} onMode={setMode} onProtocol={() => setProtoOpen(true)} />
      {mode === "mcc" ? (
        <MccView connection={<Connection />} onProtocol={() => setProtoOpen(true)} />
      ) : (
        <MonitorView connection={<Connection />} />
      )}
      {protoOpen && <ProtocolEditor onClose={() => setProtoOpen(false)} />}
      <Toasts />
    </div>
  );
}
