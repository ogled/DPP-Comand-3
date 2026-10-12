import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import "@fontsource-variable/geologica/full.css";
import "@fontsource-variable/martian-mono/wdth.css";
import "./styles/tokens.css";
import "./styles/app.css";
import App from "./App.jsx";

createRoot(document.getElementById("root")).render(
  <StrictMode>
    <App />
  </StrictMode>,
);
