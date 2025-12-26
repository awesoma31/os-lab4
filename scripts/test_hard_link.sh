#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MP="/mnt/vtfs"
MODULE_NAME="vtfs"
MODULE_FILE="$ROOT_DIR/vtfs.ko"

step() { echo ""; echo "==> $1"; }

cleanup() {
  step "Cleanup"
  sync
  sleep 0.5
  cd /
  umount -l "$MP" 2>/dev/null || true
  sleep 0.5
  rmmod "$MODULE_NAME" 2>/dev/null || true
}


if [ "$EUID" -ne 0 ]; then
  echo "Run as root: sudo $0"
  exit 1
fi

trap cleanup EXIT

step "Build (if needed)"
if [ ! -f "$MODULE_FILE" ]; then
  (cd "$ROOT_DIR" && make)
fi

step "Load module"
if ! lsmod | grep -q "^$MODULE_NAME "; then
  insmod "$MODULE_FILE"
fi

step "Mount filesystem"
mkdir -p "$MP"
if ! mountpoint -q "$MP"; then
  mount -t vtfs none "$MP"
fi

step "Reset test area"
rm -f "$MP/a" "$MP/b" "$MP/aboba" 2>/dev/null || true

pwd

step "[1] create files"
echo aboba > "$MP/aboba"
echo hello > "$MP/a"

step "[2] create hard link"
ln "$MP/a" "$MP/b"
ls -li "$MP/a" "$MP/b" "$MP/aboba"

step "[3] write through b, read through a"
echo world >> "$MP/b"
cat "$MP/a"

step "[4] unlink a"
rm "$MP/a"
stat "$MP/b"
cat "$MP/b"

step "[5] write after unlink"
echo again >> "$MP/b"
cat "$MP/b"

step "[6] unlink last link"
rm "$MP/b"
ls -la "$MP"

step "[7] check aboba state"
ls -l "$MP/aboba"

echo ""
echo "OK: hard links work correctly"
