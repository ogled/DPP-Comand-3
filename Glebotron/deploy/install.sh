#!/usr/bin/env bash
# Установка и обновление Глеботрона на сервере (Ubuntu 24.04, nginx, certbot).
#   sudo bash deploy/install.sh
# Повторный запуск безопасен: код выкладывается новым релизом, база и настройки не трогаются.
# Сайт cosmos1562.space (:5000) и Xray (:2083) скрипт не касается.
set -euo pipefail

SRC="$(cd "$(dirname "$0")/.." && pwd)"
BASE=/opt/glebotron
DATA=/var/lib/glebotron
ETC=/etc/glebotron
DOMAIN=glebotron.cosmos1562.space
SITE=/etc/nginx/sites-available/glebotron
RELEASE="$BASE/releases/$(date +%Y%m%d-%H%M%S)"

step() { echo "==> $*"; }
die() { echo "!! $*" >&2; exit 1; }

[ "$(id -u)" -eq 0 ] || die "Запустите через sudo: sudo bash $0"
[ -f "$SRC/app.py" ] || die "Нет $SRC/app.py"
[ -f "$SRC/static/index.html" ] || die "Нет собранного интерфейса: cd frontend && npm run build"

step "Пользователь и папки"
id glebotron >/dev/null 2>&1 || useradd --system --home-dir "$BASE" --shell /usr/sbin/nologin glebotron
install -d -m 755 "$BASE" "$BASE/releases"
install -d -m 750 -o glebotron -g glebotron "$DATA" "$DATA/backups"
install -d -m 750 -o root -g glebotron "$ETC"

step "Код → $RELEASE"
mkdir -p "$RELEASE"
cp "$SRC"/*.py "$SRC/requirements.txt" "$RELEASE/"
cp -r "$SRC/static" "$SRC/tools" "$SRC/deploy" "$RELEASE/"
find "$RELEASE" -name __pycache__ -prune -exec rm -rf {} +
chown -R root:root "$RELEASE"
chmod -R a+rX,go-w "$RELEASE"

step "Python venv"
if [ ! -x "$BASE/venv/bin/pip" ]; then  # без python3-venv остаётся venv без pip
  python3 -m venv "$BASE/venv" 2>/dev/null || {
    apt-get update -q && apt-get install -y -q python3-venv
    python3 -m venv "$BASE/venv"
  }
fi
REQ_HASH="$(sha256sum "$RELEASE/requirements.txt" | cut -d' ' -f1)"
if [ "$(cat "$BASE/venv/.requirements.sha256" 2>/dev/null || true)" != "$REQ_HASH" ]; then
  "$BASE/venv/bin/pip" install -q --upgrade pip
  "$BASE/venv/bin/pip" install -q -r "$RELEASE/requirements.txt"
  echo "$REQ_HASH" > "$BASE/venv/.requirements.sha256"
else
  echo "    зависимости не менялись"
fi

step "Настройки $ETC/glebotron.env"
if [ ! -f "$ETC/glebotron.env" ]; then
  KEY="$(python3 -c 'import secrets; print(secrets.token_hex(32))')"
  (umask 027; sed "s/^GLEBOTRON_SECRET_KEY=.*/GLEBOTRON_SECRET_KEY=$KEY/" "$SRC/deploy/glebotron.env.example" > "$ETC/glebotron.env")
  echo "    создан, SECRET_KEY сгенерирован"
else
  echo "    уже есть, не трогаю (новые параметры смотрите в deploy/glebotron.env.example)"
fi
chown root:glebotron "$ETC/glebotron.env"
chmod 640 "$ETC/glebotron.env"

step "systemd и команда glebotron"
install -m 644 "$SRC/deploy/glebotron.service" "$SRC/deploy/glebotron-backup.service" \
  "$SRC/deploy/glebotron-backup.timer" /etc/systemd/system/
install -m 755 "$SRC/deploy/glebotron-cli" /usr/local/bin/glebotron
systemctl daemon-reload

if [ -f "$DATA/glebotron.db" ] && [ -e "$BASE/app" ]; then
  step "Бэкап базы перед обновлением"
  /usr/local/bin/glebotron backup --keep 7
fi

step "Запуск нового релиза"
PREV="$(readlink -f "$BASE/app" 2>/dev/null || true)"
ln -sfn "$RELEASE" "$BASE/app"
systemctl enable --quiet glebotron glebotron-backup.timer
systemctl start glebotron-backup.timer
systemctl restart glebotron
ok=""
for _ in $(seq 1 30); do
  if curl -fsS http://127.0.0.1:5001/api/status >/dev/null 2>&1; then ok=1; break; fi
  sleep 0.5
done
if [ -z "$ok" ]; then
  journalctl -u glebotron -n 30 --no-pager || true
  if [ -n "$PREV" ] && [ "$PREV" != "$RELEASE" ]; then
    echo "!! Новый релиз не отвечает, возвращаю $PREV" >&2
    ln -sfn "$PREV" "$BASE/app"
    systemctl restart glebotron
  fi
  die "Глеботрон не запустился, смотрите журнал выше"
fi
echo "    сервис отвечает на 127.0.0.1:5001"

step "nginx"
if [ -d "/etc/letsencrypt/live/$DOMAIN" ]; then CONF=nginx-glebotron.conf; else CONF=nginx-glebotron.http.conf; fi
OLD=""
if [ -f "$SITE" ]; then OLD="$(mktemp)"; cp "$SITE" "$OLD"; fi
install -m 644 "$SRC/deploy/$CONF" "$SITE"
ln -sfn "$SITE" /etc/nginx/sites-enabled/glebotron
if nginx -t 2>/tmp/glebotron-nginx-test.log; then
  systemctl reload nginx
  echo "    установлен $CONF"
else
  cat /tmp/glebotron-nginx-test.log >&2
  if [ -n "$OLD" ]; then cp "$OLD" "$SITE"; else rm -f "$SITE" /etc/nginx/sites-enabled/glebotron; fi
  nginx -t >/dev/null 2>&1 && systemctl reload nginx
  die "nginx -t не прошёл, прежний конфиг возвращён"
fi
[ -n "$OLD" ] && rm -f "$OLD"

step "Старые релизы (храню 3)"
ls -1dt "$BASE"/releases/* | tail -n +4 | xargs -r rm -rf

echo
curl -fsS http://127.0.0.1:5001/api/status && echo
if [ "$CONF" = nginx-glebotron.http.conf ]; then
  cat <<MSG

Сертификата для $DOMAIN ещё нет. Выпустите его и запустите установку ещё раз:
  sudo certbot certonly --nginx -d $DOMAIN --deploy-hook "systemctl reload nginx"
  sudo bash $BASE/app/deploy/install.sh
MSG
else
  echo "Готово: https://$DOMAIN"
fi
