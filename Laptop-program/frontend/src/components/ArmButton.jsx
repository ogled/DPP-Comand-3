import { useEffect, useState } from "react";

const ARM_SECONDS = 5;

/**
 * Кнопка в два шага для опасных действий: «Взвести» → «Выполнить» (5 с), иначе сброс.
 * Без armed — обычная кнопка «Отправить».
 */
export default function ArmButton({ armed: needsArm, danger, disabled, onFire, label = "Отправить", armLabel = "Взвести", onArmChange }) {
  const [left, setLeft] = useState(0);

  useEffect(() => {
    if (!left) return undefined;
    const id = setTimeout(() => setLeft((s) => s - 1), 1000);
    return () => clearTimeout(id);
  }, [left]);

  useEffect(() => onArmChange?.(left > 0), [left > 0]); // eslint-disable-line react-hooks/exhaustive-deps

  useEffect(() => {
    if (disabled) setLeft(0);
  }, [disabled]);

  if (!needsArm) {
    return (
      <button className={`btn ${danger ? "danger" : "primary"}`} disabled={disabled} onClick={onFire}>
        {label}
      </button>
    );
  }
  if (left > 0) {
    return (
      <span className="arm-pair">
        <button className="btn armed" onClick={() => { setLeft(0); onFire(); }} autoFocus
          onKeyDown={(e) => e.key === "Escape" && setLeft(0)}>
          Выполнить <span className="arm-count">{left}</span>
        </button>
        <button className="btn quiet" onClick={() => setLeft(0)}>Отмена</button>
      </span>
    );
  }
  return (
    <button className={`btn ${danger ? "danger" : ""}`} disabled={disabled} onClick={() => setLeft(ARM_SECONDS)}>
      {armLabel}
    </button>
  );
}
