/* Значки линиями 1.6 px, цвет — currentColor. */
const PATHS = {
  check: <path d="M3.5 8.5l3 3 6-7" />,
  check2: (
    <>
      <path d="M1.5 8.5l3 3 6-7" />
      <path d="M8 11.5l.5.5 6-7.5" />
    </>
  ),
  clock: (
    <>
      <circle cx="8" cy="8" r="5.6" />
      <path d="M8 4.8V8l2.2 1.6" />
    </>
  ),
  antenna: (
    <>
      <path d="M8 9v5.5M5.5 14.5h5" />
      <circle cx="8" cy="7.5" r="1.3" />
      <path d="M5.2 4.7a4 4 0 000 5.6M10.8 4.7a4 4 0 010 5.6M3.3 2.8a6.6 6.6 0 000 9.4M12.7 2.8a6.6 6.6 0 010 9.4" />
    </>
  ),
  sat: (
    <>
      <rect x="6" y="6" width="4" height="4" rx="0.6" transform="rotate(45 8 8)" />
      <path d="M5 5L2.2 2.2M11 11l2.8 2.8M1.5 5.2L5.2 1.5M10.8 14.5l3.7-3.7" />
    </>
  ),
  warn: (
    <>
      <path d="M8 2.2l6.3 11.3H1.7z" />
      <path d="M8 6.5v3.2M8 11.6v.1" />
    </>
  ),
  send: <path d="M2.5 8h9M8 3.5L12.5 8 8 12.5" />,
  back: <path d="M10 3L5 8l5 5" />,
  copy: (
    <>
      <rect x="5.5" y="5.5" width="8" height="8" rx="1.2" />
      <path d="M10.5 3.5v-.8c0-.7-.5-1.2-1.2-1.2H3.7c-.7 0-1.2.5-1.2 1.2v5.6c0 .7.5 1.2 1.2 1.2h.8" />
    </>
  ),
  sun: (
    <>
      <circle cx="8" cy="8" r="2.8" />
      <path d="M8 1.5v1.6M8 12.9v1.6M1.5 8h1.6M12.9 8h1.6M3.4 3.4l1.1 1.1M11.5 11.5l1.1 1.1M3.4 12.6l1.1-1.1M11.5 4.5l1.1-1.1" />
    </>
  ),
  moon: <path d="M13 9.8A5.5 5.5 0 016.2 3a5.5 5.5 0 106.8 6.8z" />,
  exit: <path d="M9.5 3H3.5v10h6M7 8h7M11.5 5.5L14 8l-2.5 2.5" />,
  plus: <path d="M8 3v10M3 8h10" />,
  terminal: (
    <>
      <rect x="4" y="5" width="8" height="9.5" rx="1.2" />
      <path d="M8 5V1.5M6.2 8h3.6M6.2 10.5h3.6" />
    </>
  ),
  user: (
    <>
      <circle cx="8" cy="5.5" r="2.8" />
      <path d="M2.5 14.5c.6-2.8 2.8-4.3 5.5-4.3s4.9 1.5 5.5 4.3" />
    </>
  ),
};

export default function Icon({ name, size = 16, title }) {
  return (
    <svg className="icon" width={size} height={size} viewBox="0 0 16 16" fill="none" stroke="currentColor"
      strokeWidth="1.6" strokeLinecap="round" strokeLinejoin="round" aria-hidden={title ? undefined : true}
      role={title ? "img" : undefined}>
      {title && <title>{title}</title>}
      {PATHS[name]}
    </svg>
  );
}
