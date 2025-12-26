#!/bin/bash
set -euo pipefail

# ------------------------------
# Config
# ------------------------------
MODULE_NAME="vtfs"
MOUNT_POINT="/mnt/vtfs"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MODULE_FILE="$ROOT_DIR/${MODULE_NAME}.ko"

# ------------------------------
# Helpers
# ------------------------------
step() {
  echo ""
  echo "==> $1"
}

die() {
  echo "ERROR: $1"
  exit 1
}

cleanup() {
  echo ""
  echo "==> Cleanup"

  sync
  sleep 0.5
  cd /

  if mountpoint -q "$MOUNT_POINT" 2>/dev/null; then
    umount -l "$MOUNT_POINT" || true
    sleep 0.5
  fi

  if lsmod | grep -q "^$MODULE_NAME "; then
    rmmod "$MODULE_NAME" || true
  fi
}

# Cleanup on Ctrl+C or kill
trap cleanup INT TERM

# ------------------------------
# Root check
# ------------------------------
if [ "$EUID" -ne 0 ]; then
  die "Run as root: sudo $0"
fi

# ------------------------------
# Build
# ------------------------------
step "Build module"
cd "$ROOT_DIR"
make

[ -f "$MODULE_FILE" ] || die "Module not built"

# ------------------------------
# Load module
# ------------------------------
step "Load module"
if ! lsmod | grep -q "^$MODULE_NAME "; then
  insmod "$MODULE_FILE"
fi

# ------------------------------
# Mount filesystem
# ------------------------------
step "Mount filesystem"
mkdir -p "$MOUNT_POINT"
if ! mountpoint -q "$MOUNT_POINT"; then
  mount -t vtfs none "$MOUNT_POINT"
fi

# ------------------------------
# Reset FS state
# ------------------------------
step "Reset filesystem state"
rm -rf "$MOUNT_POINT"/* 2>/dev/null || true

# ============================================================
# TESTS START HERE
# ============================================================

# ------------------------------
# Test 1: file create / read / write
# ------------------------------
step "Test 1: file create / read / write"

echo "hello" > "$MOUNT_POINT/file1"
cat "$MOUNT_POINT/file1" | grep -q "hello" || die "file1 read failed"

echo "world" >> "$MOUNT_POINT/file1"
cat "$MOUNT_POINT/file1" | grep -q "world" || die "file1 append failed"

# ------------------------------
# Test 2: directory create
# ------------------------------
step "Test 2: directory create"

mkdir "$MOUNT_POINT/dir1"
[ -d "$MOUNT_POINT/dir1" ] || die "dir1 not created"

# ------------------------------
# Test 3: nested file
# ------------------------------
step "Test 3: nested file"

echo "nested" > "$MOUNT_POINT/dir1/nested.txt"
cat "$MOUNT_POINT/dir1/nested.txt" | grep -q "nested" || die "nested file failed"

# ------------------------------
# Test 4: hard link
# ------------------------------
step "Test 4: hard link"

echo "link-test" > "$MOUNT_POINT/a"
ln "$MOUNT_POINT/a" "$MOUNT_POINT/b"

INO_A=$(ls -i "$MOUNT_POINT/a" | awk '{print $1}')
INO_B=$(ls -i "$MOUNT_POINT/b" | awk '{print $1}')

[ "$INO_A" = "$INO_B" ] || die "inode differs for hard link"

# ------------------------------
# Test 5: write via link, read original
# ------------------------------
step "Test 5: write via link, read original"

echo "via-b" >> "$MOUNT_POINT/b"
cat "$MOUNT_POINT/a" | grep -q "via-b" || die "hard link write failed"

# ------------------------------
# Test 6: unlink first link
# ------------------------------
step "Test 6: unlink first link"

rm "$MOUNT_POINT/a"
cat "$MOUNT_POINT/b" | grep -q "via-b" || die "data lost after unlink"

# ------------------------------
# Test 7: unlink last link
# ------------------------------
step "Test 7: unlink last link"

rm "$MOUNT_POINT/b"
[ ! -e "$MOUNT_POINT/b" ] || die "file still exists after last unlink"

# ------------------------------
# Test 8: remove nested dir
# ------------------------------
step "Test 8: remove nested dir"

rm "$MOUNT_POINT/dir1/nested.txt"
rmdir "$MOUNT_POINT/dir1"
[ ! -e "$MOUNT_POINT/dir1" ] || die "dir1 not removed"

# ------------------------------
# Test 9: FS still usable
# ------------------------------
step "Test 9: filesystem still usable"

echo "final" > "$MOUNT_POINT/final.txt"
cat "$MOUNT_POINT/final.txt" | grep -q "final" || die "FS unusable after operations"

# ============================================================
# TESTS END
# ============================================================

echo ""
echo "ALL VTFS TESTS PASSED SUCCESSFULLY"

# Explicit cleanup (better than EXIT trap)
cleanup
exit 0
