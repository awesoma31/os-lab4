#!/bin/bash
set -e
MP=/mnt/vtfs

echo "[1] create files"
echo aboba > $MP/aboba
echo hello > $MP/a

echo "[2] create hard link"
ln $MP/a $MP/b 
ls -li $MP/a $MP/b $MP/aboba 

echo "[3] write through b, read through a"
echo world >> $MP/b
cat $MP/a

echo "[4] unlink a"
rm $MP/a
stat $MP/b
cat $MP/b

echo "[5] write after unlink"
echo again >> $MP/b
cat $MP/b

echo "[6] unlink last link"
rm $MP/b
ls -l $MP

echo "[7] check aboba state"
ls -l $MP/aboba

echo "OK: hard links work correctly"
