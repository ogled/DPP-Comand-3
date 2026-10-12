/* Запросы к app.py. Ответ всегда { ok, ... } — при сетевой ошибке тоже. */

async function request(method, path, body) {
  let r;
  try {
    r = await fetch(path, {
      method,
      credentials: "same-origin",
      headers: body === undefined ? {} : { "Content-Type": "application/json" },
      body: body === undefined ? undefined : JSON.stringify(body),
    });
  } catch {
    return { ok: false, offline: true, error: "Нет связи с сервером" };
  }
  let data;
  try {
    data = await r.json();
  } catch {
    data = { ok: false, error: `Сервер ответил ${r.status}` };
  }
  return { ...data, httpStatus: r.status };
}

export const get = (path) => request("GET", path);
export const post = (path, body = {}) => request("POST", path, body);
