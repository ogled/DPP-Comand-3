import { useEffect } from "react";
import { boot, useStore } from "./store.js";
import AuthView from "./components/AuthView.jsx";
import ChatList from "./components/ChatList.jsx";
import ChatView from "./components/ChatView.jsx";
import StatusBar from "./components/StatusBar.jsx";
import Toasts from "./components/Toasts.jsx";

export default function App() {
  const phase = useStore((s) => s.phase);
  const active = useStore((s) => s.active);

  useEffect(() => {
    boot();
  }, []);

  if (phase === "loading") return <div className="loading" aria-busy="true" />;
  return (
    <>
      {phase === "auth" ? (
        <AuthView />
      ) : (
        <div className={`app ${active != null ? "has-chat" : ""}`}>
          <StatusBar />
          <div className="workspace">
            <ChatList />
            <ChatView />
          </div>
        </div>
      )}
      <Toasts />
    </>
  );
}
