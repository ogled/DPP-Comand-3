// Тема до отрисовки, чтобы не мигало. Отдельным файлом: CSP сайта запрещает встроенные скрипты.
try {
  var t = localStorage.getItem("glebotron-theme");
  if (t !== "day" && t !== "night") t = matchMedia("(prefers-color-scheme: dark)").matches ? "night" : "day";
  document.documentElement.dataset.theme = t;
} catch (e) {
  document.documentElement.dataset.theme = "day";
}
