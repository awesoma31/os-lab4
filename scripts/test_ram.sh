#!/bin/bash
# VTFS filesystem test
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"


set -e

MODULE_NAME="vtfs"
MODULE_FILE="${MODULE_NAME}.ko"
MOUNT_POINT="/mnt/vtfs"

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

echo "VTFS RAM mode test"

step "Build check"
if [ ! -f "$MODULE_FILE" ]; then
  echo "Module not found, running make"
  make
else
  echo "Module already built, skipping make"
fi

step "Module load"
if lsmod | grep -q "^$MODULE_NAME "; then
  echo "Module already loaded"
else
  echo "Loading module: insmod $MODULE_FILE"
  insmod "$MODULE_FILE"
fi

step "Mount filesystem"
mkdir -p "$MOUNT_POINT"
if mountpoint -q "$MOUNT_POINT"; then
  echo "$MOUNT_POINT already mounted"
else
  mount -t vtfs none "$MOUNT_POINT" -o token=""
fi

pwd

# file
step "Test: file creation"
echo "hello" > "$MOUNT_POINT/file.txt"
ls -l "$MOUNT_POINT/file.txt"
cat "$MOUNT_POINT/file.txt"

# dir
step "Test: directory creation"
mkdir "$MOUNT_POINT/dir"
ls -ld "$MOUNT_POINT/dir"

# nested 
step "Test: file inside directory"
echo "nested" > "$MOUNT_POINT/dir/nested.txt"
ls -l "$MOUNT_POINT/dir/nested.txt"
cat "$MOUNT_POINT/dir/nested.txt"

# rm
step "Test: removal"
rm "$MOUNT_POINT/file.txt"
rm "$MOUNT_POINT/dir/nested.txt"
rmdir "$MOUNT_POINT/dir"
ls -la "$MOUNT_POINT"

echo ""
echo "VTFS RAM mode test PASSED"
