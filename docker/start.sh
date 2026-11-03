#!/bin/sh
# The demo desktop: Xvfb, a window manager, Chromium, and jumpseat on :8080.
set -e
export DISPLAY=:0
Xvfb :0 -screen 0 1280x800x24 -nolisten tcp &
for _ in $(seq 50); do [ -S /tmp/.X11-unix/X0 ] && break; sleep 0.1; done
# The wallpaper fills the screen, and fills it again whenever the screen is
# resized: the root window keeps a pixmap of the old size and would tile it.
wallpaper() { hsetroot -fill /usr/share/jumpseat/wallpaper.png >/dev/null 2>&1 || true; }
wallpaper
xev -root -event randr 2>/dev/null | while read -r line; do
  case "$line" in RRScreenChangeNotify*) wallpaper ;; esac
done &
openbox &
chromium --no-sandbox --disable-dev-shm-usage --no-first-run --start-maximized \
  --user-data-dir=/tmp/chromium "${START_URL:-https://example.com}" >/dev/null 2>&1 &
exec jumpseat --listen :8080 --dir /tmp/jumpseat
