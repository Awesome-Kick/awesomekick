#!/usr/bin/env bash
# =============================================================================
#  AwesomeKick – build and start the KICK simulator (KT_KickEM) on Linux
# =============================================================================
#  Run from the repository, as a normal user (NOT with sudo):
#
#      bash scripts/setup-linux.sh              # pick the mode automatically
#      bash scripts/setup-linux.sh --desktop    # normal window on this desktop
#      bash scripts/setup-linux.sh --headless   # virtual display + browser (noVNC)
#      add --password to protect the browser view with a password (it is open to your network otherwise)
#
#  1. installs the build dependencies (asks for the sudo password once)
#  2. builds the simulator with all CPU cores
#  3. writes helper scripts to ~/kick-dev:
#        kickem-desktop.sh  kickem-start.sh  kickem-stop.sh  kickem-rebuild.sh
#  4. starts the simulator
#
#  Output is also written to ~/kick-dev/setup.log
#  Tested on Ubuntu 24.04 (x86_64).
# =============================================================================
set -euo pipefail

REPO_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )/.." && pwd )"
BUILD_DIR="$REPO_DIR/Kick/build/sim"
BIN="$BUILD_DIR/apps/kickem/KT_KickEM_artefacts/Release/KT_KickEM"
DEV_DIR="$HOME/kick-dev"
CONF_DIR="$HOME/.config/kickem"
WEB_PORT=6080          # noVNC in the browser (headless mode)
VNC_PORT=5942          # internal, localhost only
AUDIO_PORT=6081        # sound stream to the browser (headless mode)
DISPLAY_NUM=42         # virtual display :42 (headless mode)

say()  { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
ok()   { printf '\033[1;32m    ✓ %s\033[0m\n' "$*"; }
fail() { printf '\n\033[1;31m!!! %s\033[0m\n' "$*" >&2; exit 1; }

# --- mode ---------------------------------------------------------------------
MODE=""
NO_PASSWORD=1
for arg in "$@"; do
  case "$arg" in
    --desktop)  MODE=desktop ;;
    --headless) MODE=headless ;;
    --password) NO_PASSWORD=0 ;;
    --no-password) NO_PASSWORD=1 ;;
    -h|--help)  sed -n '2,20p' "$0"; exit 0 ;;
    *)          fail "Unknown option: $arg (use --desktop or --headless)" ;;
  esac
done
if [ -z "$MODE" ]; then
  # A local desktop session has WAYLAND_DISPLAY, or DISPLAY without being an SSH login
  # (ssh -X also sets DISPLAY, but then the window would travel over the network).
  if [ -n "${WAYLAND_DISPLAY:-}" ] || { [ -n "${DISPLAY:-}" ] && [ -z "${SSH_CONNECTION:-}" ]; }; then
    MODE=desktop
  else
    MODE=headless
  fi
fi

# --- checks ---------------------------------------------------------------------
[ "$(id -u)" -eq 0 ] && fail "Please do NOT run this as root / with sudo. Run: bash scripts/setup-linux.sh"
command -v apt-get >/dev/null || fail "apt-get not found – this script supports Ubuntu / Debian."
[ -f "$REPO_DIR/Kick/CMakeLists.txt" ] || fail "Run this script from the AwesomeKick repository (Kick/ not found)."
mkdir -p "$DEV_DIR/logs" "$CONF_DIR"
exec > >(tee -a "$DEV_DIR/setup.log") 2>&1
say "AwesomeKick setup $(date '+%Y-%m-%d %H:%M') on $(hostname) – $(nproc) CPU cores, $(lsb_release -ds 2>/dev/null || echo Linux), mode: $MODE"

if [ "$MODE" = headless ] && grep -q ":$WEB_PORT " <(ss -ltn 2>/dev/null); then
  pgrep -u "$USER" -f "websockify --web .* $WEB_PORT" >/dev/null \
    || fail "Port $WEB_PORT is already used by another service. Change WEB_PORT in this script."
fi
if [ "$MODE" = headless ] && grep -q ":$AUDIO_PORT " <(ss -ltn 2>/dev/null); then
  pgrep -u "$USER" -f "awesomekick-audio.py" >/dev/null \
    || fail "Port $AUDIO_PORT is already used by another service. Change AUDIO_PORT in this script."
fi

# --- 1. packages ----------------------------------------------------------------
say "1/4 Installing packages (asks for your sudo password)"
PKGS=( build-essential cmake ninja-build git pkg-config
       libasound2-dev libjack-jackd2-dev libcurl4-openssl-dev
       libfreetype-dev libfontconfig1-dev
       libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev
       libxrandr-dev libxrender-dev libxi-dev libglu1-mesa-dev mesa-common-dev
       libserial-dev fonts-dejavu-core )
[ "$MODE" = headless ] && PKGS+=( xvfb x11vnc novnc python3-websockify x11-utils x11-xserver-utils openssl jackd2 ffmpeg )
sudo apt-get update -qq
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq "${PKGS[@]}"
ok "Packages installed"

# --- 2. build -------------------------------------------------------------------
say "2/4 Building the simulator – takes a few minutes the first time"
cmake -S "$REPO_DIR/Kick" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DKICK_SIMULATOR=ON -DTIMESTAMP=OFF > "$DEV_DIR/cmake.log" 2>&1 \
  || { tail -40 "$DEV_DIR/cmake.log"; fail "CMake configuration failed (see $DEV_DIR/cmake.log)"; }
time cmake --build "$BUILD_DIR" --target KT_KickEM -j"$(nproc)" \
  || fail "Build failed – the last lines above show why"
[ -x "$BIN" ] || fail "Executable not found: $BIN"
# Never start a binary without the safety sandbox (see README).
grep -q "__wrap_system" <(nm -C "$BIN" 2>/dev/null) \
  || fail "The safety sandbox is missing from the executable – do NOT start it"
ok "Built: $BIN (safety sandbox active)"

# --- 3. helper scripts ----------------------------------------------------------
say "3/4 Writing helper scripts to $DEV_DIR"
echo "$MODE" > "$CONF_DIR/mode"

cat > "$DEV_DIR/kickem-desktop.sh" <<EOF
#!/usr/bin/env bash
# Start the AwesomeKick simulator as a normal window on this desktop.
set -u
[ "\$(id -u)" -eq 0 ] && { echo "Do not run the simulator as root."; exit 1; }
LOG="$DEV_DIR/logs"; mkdir -p "\$LOG"
if pgrep -u "\$USER" -x KT_KickEM >/dev/null; then
  echo "The simulator is already running (stop it with $DEV_DIR/kickem-stop.sh)."; exit 0
fi
cd "$(dirname "$BIN")"
exec "$BIN" >"\$LOG/kickem.log" 2>&1 < /dev/null
EOF

# Browser access: open by default (meant for a home network); --password protects it.
# (x11vnc itself only listens on localhost, the browser reaches it through websockify on $WEB_PORT.)
if [ "$NO_PASSWORD" = 1 ]; then
  VNC_AUTH="-nopw"
  PASSWORD_INFO="none (use --password to set one)"
else
  VNC_AUTH="-rfbauth \"$CONF_DIR/vncpass\""
  PASSWORD_INFO="\$(cat \"$CONF_DIR/vncpass.txt\")"
fi

cat > "$DEV_DIR/kickem-start.sh" <<EOF
#!/usr/bin/env bash
# Start the AwesomeKick simulator on the virtual display :$DISPLAY_NUM and share it to the browser:
#   http://<this machine>:$WEB_PORT/   (picture via noVNC, sound via JACK -> awesomekick-audio.py on port $AUDIO_PORT)
set -u
[ "\$(id -u)" -eq 0 ] && { echo "Do not run the simulator as root."; exit 1; }
LOG="$DEV_DIR/logs"; mkdir -p "\$LOG"
"$DEV_DIR/kickem-stop.sh" >/dev/null 2>&1
# Wait until the old instance is really gone – otherwise the new Xvfb fails with
# "Server is already active for display $DISPLAY_NUM" and the simulator has no screen.
old_running() { pgrep -u "\$USER" -f "^Xvfb :$DISPLAY_NUM" || pgrep -u "\$USER" -x KT_KickEM || pgrep -u "\$USER" -f "^x11vnc -display :$DISPLAY_NUM" || pgrep -u "\$USER" -f "websockify --web .* $WEB_PORT" || pgrep -u "\$USER" -f "^jackd --no-realtime" || pgrep -u "\$USER" -f "^python3 -I .*awesomekick-audio.py"; }
for i in \$(seq 1 40); do old_running >/dev/null || break; sleep 0.25; done
if old_running >/dev/null; then
  pkill -KILL -u "\$USER" -f "^Xvfb :$DISPLAY_NUM"; pkill -KILL -u "\$USER" -x KT_KickEM
  pkill -KILL -u "\$USER" -f "^jackd --no-realtime"; pkill -KILL -u "\$USER" -f "^ffmpeg .* awesomekick_stream"; sleep 0.5
fi
setsid nohup Xvfb :$DISPLAY_NUM -screen 0 1920x1080x24 -nolisten tcp >"\$LOG/xvfb.log" 2>&1 < /dev/null &
for i in \$(seq 1 40); do xdpyinfo -display :$DISPLAY_NUM >/dev/null 2>&1 && break; sleep 0.25; done
xdpyinfo -display :$DISPLAY_NUM >/dev/null 2>&1 || { echo "ERROR: virtual display :$DISPLAY_NUM did not start – see \$LOG/xvfb.log"; exit 1; }
# A normal arrow pointer (Xvfb's default is an "X" cross, which the simulator window inherits)
xsetroot -display :$DISPLAY_NUM -cursor_name left_ptr 2>/dev/null
# Sound: a JACK server without hardware (dummy driver, no root or audio group needed). The simulator
# plays into it, awesomekick-audio.py captures it and streams it to the browser page.
# Leftovers of a JACK server or client that was killed make the next server hang on start-up.
pgrep -u "\$USER" -x jackd >/dev/null || rm -rf /dev/shm/jack_sem.\$(id -u)_* /dev/shm/jack_db-\$(id -u)
setsid nohup jackd --no-realtime -t 2000 -d dummy -r 48000 -p 512 >"\$LOG/jackd.log" 2>&1 < /dev/null &
for i in \$(seq 1 40); do jack_lsp >/dev/null 2>&1 && break; sleep 0.25; done
jack_lsp >/dev/null 2>&1 || echo "WARNING: JACK did not start – no sound in the browser (see \$LOG/jackd.log)"
setsid nohup python3 -I "$REPO_DIR/scripts/awesomekick-audio.py" --port $AUDIO_PORT >"\$LOG/audio.log" 2>&1 < /dev/null &
cd "$(dirname "$BIN")"
DISPLAY=:$DISPLAY_NUM KICKEM_KIOSK=1 KICKEM_VIEW="\${KICKEM_VIEW:-studio}" KICKEM_AUDIO_TYPE=JACK setsid nohup "$BIN" >"\$LOG/kickem.log" 2>&1 < /dev/null &
setsid nohup x11vnc -display :$DISPLAY_NUM -rfbport $VNC_PORT -localhost -forever -shared -cursor arrow \\
      $VNC_AUTH -quiet >"\$LOG/x11vnc.log" 2>&1 < /dev/null &
# Web root: noVNC plus our index.html (noVNC view + "Sound on" button), so http://<host>:$WEB_PORT/ is all you need
WEB="$DEV_DIR/novnc-web"; mkdir -p "\$WEB"
for f in /usr/share/novnc/*; do ln -sfn "\$f" "\$WEB/\$(basename "\$f")"; done
cp "$REPO_DIR/scripts/web/index.html" "\$WEB/index.html"
setsid nohup websockify --web "\$WEB" $WEB_PORT localhost:$VNC_PORT >"\$LOG/novnc.log" 2>&1 < /dev/null &
sleep 2
IP=\$(hostname -I | awk '{print \$1}')
echo
echo "AwesomeKick simulator is running."
echo "  Open in a browser: http://\$IP:$WEB_PORT/"
echo "  Password:          $PASSWORD_INFO"
echo "  Sound:             on by default – your first click on the page turns it on"
echo "  Stop:              $DEV_DIR/kickem-stop.sh"
echo "  Logs:              \$LOG/"
echo "  Blocked commands:  grep sandbox \$LOG/kickem.log"
EOF

cat > "$DEV_DIR/kickem-stop.sh" <<EOF
#!/usr/bin/env bash
# Stop the simulator and, in headless mode, the virtual display and browser sharing.
# JACK goes first: stopping its clients first makes it wait ~20 s for each of them.
pkill -u "\$USER" -f "^jackd --no-realtime" 2>/dev/null
pkill -u "\$USER" -f "websockify --web .* $WEB_PORT" 2>/dev/null
pkill -u "\$USER" -f "^x11vnc -display :$DISPLAY_NUM" 2>/dev/null
pkill -u "\$USER" -x KT_KickEM 2>/dev/null
pkill -u "\$USER" -f "^Xvfb :$DISPLAY_NUM" 2>/dev/null
pkill -u "\$USER" -f "^python3 -I .*awesomekick-audio.py" 2>/dev/null
pkill -u "\$USER" -f "^ffmpeg .* awesomekick_stream" 2>/dev/null
echo "AwesomeKick simulator stopped."
EOF

cat > "$DEV_DIR/kickem-rebuild.sh" <<EOF
#!/usr/bin/env bash
# Rebuild after code changes and restart in the mode chosen at setup ($CONF_DIR/mode).
set -e
cmake --build "$BUILD_DIR" --target KT_KickEM -j"\$(nproc)"
if [ "\$(cat "$CONF_DIR/mode" 2>/dev/null)" = desktop ]; then
  "$DEV_DIR/kickem-stop.sh" >/dev/null
  for i in \$(seq 1 40); do pgrep -u "\$USER" -x KT_KickEM >/dev/null || break; sleep 0.25; done
  setsid nohup "$DEV_DIR/kickem-desktop.sh" >/dev/null 2>&1 < /dev/null &
  echo "AwesomeKick simulator restarted as a window."
else
  "$DEV_DIR/kickem-start.sh"
fi
EOF
chmod +x "$DEV_DIR"/kickem-*.sh
ok "Scripts: kickem-desktop.sh | kickem-start.sh | kickem-stop.sh | kickem-rebuild.sh"

if [ "$MODE" = headless ]; then
  if [ "$NO_PASSWORD" = 1 ]; then
    ok "Browser access without password (--password to set one)"
  else
    if [ ! -f "$CONF_DIR/vncpass" ]; then
      VNC_PW="$(openssl rand -hex 4)"
      x11vnc -storepasswd "$VNC_PW" "$CONF_DIR/vncpass" >/dev/null 2>&1
      echo "$VNC_PW" > "$CONF_DIR/vncpass.txt"
      chmod 600 "$CONF_DIR/vncpass" "$CONF_DIR/vncpass.txt"
    fi
    ok "Browser password stored in $CONF_DIR/vncpass.txt"
  fi
else
  APPS="$HOME/.local/share/applications"; mkdir -p "$APPS"
  cat > "$APPS/awesomekick.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=AwesomeKick
Comment=KICK groovebox simulator (community build)
Exec=$DEV_DIR/kickem-desktop.sh
Icon=$REPO_DIR/scripts/awesomekick.svg
Terminal=false
Categories=AudioVideo;Audio;Music;
EOF
  command -v update-desktop-database >/dev/null && update-desktop-database "$APPS" >/dev/null 2>&1 || true
  ok "Application menu entry: AwesomeKick"
fi

# --- 4. start -------------------------------------------------------------------
say "4/4 Starting the simulator"
if [ "$MODE" = desktop ]; then
  "$DEV_DIR/kickem-stop.sh" >/dev/null
  setsid nohup "$DEV_DIR/kickem-desktop.sh" >/dev/null 2>&1 < /dev/null &
  echo
  echo "AwesomeKick simulator is starting as a window."
  echo "  Start again later: application menu → AwesomeKick, or $DEV_DIR/kickem-desktop.sh"
  echo "  Logs:              $DEV_DIR/logs/"
  echo "  Blocked commands:  grep sandbox $DEV_DIR/logs/kickem.log"
else
  "$DEV_DIR/kickem-start.sh"
fi
