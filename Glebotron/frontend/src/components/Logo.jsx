/* Знак Глеботрона: спутник с панелями и лампой. Цвета — в app.css (.logo-*). */
export default function Logo({ size = 28 }) {
  return (
    <svg className="logo" width={size} height={size} viewBox="0 0 32 32" aria-hidden="true">
      <rect className="logo-plate" width="32" height="32" rx="7" />
      <g className="logo-sat" fill="none" strokeWidth="2" strokeLinecap="round">
        <rect x="12" y="12" width="8" height="8" rx="1.5" transform="rotate(45 16 16)" />
        <path d="M9 9L6 6M23 23l3 3M6 12l6-6M20 26l6-6" />
      </g>
      <circle className="logo-lamp" cx="16" cy="16" r="1.6" />
    </svg>
  );
}
