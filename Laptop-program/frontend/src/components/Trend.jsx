import { useEffect, useMemo, useState } from "react";
import { getSeries, nowSec, useStation, usePersistent } from "../store.js";
import { fmtClock, fmtNumber } from "../format.js";

const WINDOWS = [
  [60, "1 мин"],
  [300, "5 мин"],
  [900, "15 мин"],
  [0, "Всё"],
];
const PAD = { l: 52, r: 14, t: 12, b: 24 };

function niceTicks(lo, hi, count = 5) {
  const span = hi - lo || 1;
  const raw = span / count;
  const mag = 10 ** Math.floor(Math.log10(raw));
  const step = [1, 2, 2.5, 5, 10].map((m) => m * mag).find((s) => s >= raw) || raw;
  const out = [];
  for (let v = Math.ceil(lo / step) * step; v <= hi + step * 1e-9; v += step) out.push(+v.toFixed(10));
  return out;
}

function useSize() {
  const [el, setEl] = useState(null);
  const [size, setSize] = useState({ w: 600, h: 200 });
  useEffect(() => {
    if (!el) return undefined;
    const ro = new ResizeObserver(([e]) =>
      setSize({ w: Math.max(240, Math.floor(e.contentRect.width)), h: Math.max(150, Math.floor(e.contentRect.height)) }));
    ro.observe(el);
    return () => ro.disconnect();
  }, [el]);
  return [size, setEl];
}

export default function Trend({ unit, field }) {
  useStation((s) => s.seriesRev); // перерисовка при новых точках
  const [win, setWin] = usePersistent("dpp.trendWindow", 300);
  const [hover, setHover] = useState(null);
  const [{ w: W, h: H }, box] = useSize();

  const data = useMemo(() => {
    if (!unit || !field) return null;
    const s = getSeries(unit.key, field.name);
    const from = win ? nowSec() - win : -Infinity;
    let i = 0;
    while (i < s.t.length && s.t[i] < from) i++;
    return { t: s.t.slice(i), v: s.v.slice(i) };
  }, [unit, field, win, unit?.ts]); // eslint-disable-line react-hooks/exhaustive-deps

  if (!unit || !field) {
    return (
      <section className="panel trend">
        <header className="panel-head"><h2>График</h2></header>
        <p className="empty-note">Выберите параметр на табло или в приборах — здесь появится его график.</p>
      </section>
    );
  }

  const { t, v } = data;
  const lim = field.limits || {};
  let lo = Math.min(...v), hi = Math.max(...v);
  if (!v.length) { lo = 0; hi = 1; }
  const span0 = hi - lo || Math.abs(hi) || 1;
  for (const x of Object.values(lim)) if (x >= lo - span0 && x <= hi + span0) { lo = Math.min(lo, x); hi = Math.max(hi, x); }
  const padY = (hi - lo || span0) * 0.1;
  lo -= padY; hi += padY;

  // окно — это максимум: пока данных меньше окна, линия занимает всю ширину
  const t1 = win ? nowSec() : t[t.length - 1] ?? nowSec();
  const t0 = Math.max(win ? t1 - win : -Infinity, t[0] ?? t1 - 60);
  const x = (ts) => PAD.l + ((ts - t0) / (t1 - t0 || 1)) * (W - PAD.l - PAD.r);
  const y = (val) => PAD.t + (1 - (val - lo) / (hi - lo || 1)) * (H - PAD.t - PAD.b);
  const path = t.map((ts, i) => `${i ? "L" : "M"}${x(ts).toFixed(1)},${y(v[i]).toFixed(1)}`).join("");
  const ticks = niceTicks(lo, hi);
  const xTicks = [t0, (t0 + t1) / 2, t1];

  const stats = v.length
    ? { min: Math.min(...v), max: Math.max(...v), avg: v.reduce((a, b) => a + b, 0) / v.length }
    : null;

  const onMove = (e) => {
    if (!t.length) return;
    const rect = e.currentTarget.getBoundingClientRect();
    const ts = t0 + ((e.clientX - rect.left - PAD.l) / (W - PAD.l - PAD.r)) * (t1 - t0);
    let best = 0;
    for (let i = 1; i < t.length; i++) if (Math.abs(t[i] - ts) < Math.abs(t[best] - ts)) best = i;
    setHover(best);
  };

  const hv = hover != null && hover < t.length ? hover : null;

  return (
    <section className="panel trend">
      <header className="panel-head">
        <h2>
          {field.name}
          <span className={`trend-now st-${field.state}`}>{field.text}{field.unit && <small>{field.unit}</small>}</span>
        </h2>
        <div className="seg" role="group" aria-label="Окно графика">
          {WINDOWS.map(([s, l]) => (
            <button key={s} className={win === s ? "on" : ""} onClick={() => setWin(s)}>{l}</button>
          ))}
        </div>
      </header>
      <div className="trend-body">
        <div className="plot" ref={box}>
        {v.length < 2 ? (
          <p className="empty-note">Копим данные: {v.length} из 2 точек для линии.</p>
        ) : (
          <svg width={W} height={H} className="trend-svg" onMouseMove={onMove} onMouseLeave={() => setHover(null)}
            role="img" aria-label={`График: ${field.name}`}>
            {ticks.map((tk) => (
              <g key={tk}>
                <line className="grid" x1={PAD.l} x2={W - PAD.r} y1={y(tk)} y2={y(tk)} />
                <text className="axis" x={PAD.l - 8} y={y(tk) + 4} textAnchor="end">{fmtNumber(tk)}</text>
              </g>
            ))}
            {xTicks.map((tk, i) => (
              <text key={i} className="axis" x={x(tk)} y={H - 6} textAnchor={i === 0 ? "start" : i === 2 ? "end" : "middle"}>
                {fmtClock(new Date(tk * 1000))}
              </text>
            ))}
            {Object.entries(lim).map(([k, val]) => val >= lo && val <= hi && (
              <g key={k}>
                <line className={`limit ${k.startsWith("crit") ? "crit" : "warn"}`} x1={PAD.l} x2={W - PAD.r} y1={y(val)} y2={y(val)} />
                <text className={`limit-label ${k.startsWith("crit") ? "crit" : "warn"}`} x={W - PAD.r - 2} y={y(val) - 4} textAnchor="end">
                  {k.startsWith("crit") ? "авария" : "внимание"} {fmtNumber(val)}
                </text>
              </g>
            ))}
            <path className="line" d={path} />
            <circle className="last" cx={x(t[t.length - 1])} cy={y(v[v.length - 1])} r="3" />
            {hv !== null && (
              <g className="cross">
                <line x1={x(t[hv])} x2={x(t[hv])} y1={PAD.t} y2={H - PAD.b} />
                <circle cx={x(t[hv])} cy={y(v[hv])} r="3.5" />
                <text x={Math.min(x(t[hv]) + 8, W - 120)} y={PAD.t + 12}>
                  {fmtNumber(v[hv])} {field.unit} в {fmtClock(new Date(t[hv] * 1000))}
                </text>
              </g>
            )}
          </svg>
        )}
        </div>
        {stats && (
          <dl className="trend-stats">
            <div><dt>Мин</dt><dd>{fmtNumber(stats.min)}</dd></div>
            <div><dt>Макс</dt><dd>{fmtNumber(stats.max)}</dd></div>
            <div><dt>Среднее</dt><dd>{fmtNumber(stats.avg)}</dd></div>
            <div><dt>Точек</dt><dd>{v.length}</dd></div>
          </dl>
        )}
      </div>
    </section>
  );
}
