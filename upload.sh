#!/bin/sh

BIN_DIR=./out/bin

echo ""
echo "============== fastboot menu ==============="
echo "  1: boot boot.img"
echo "  2: boot recovery.img"
echo "  3: flash boot.img"
echo "  4: flash recovery.img"
echo ""
echo "  9: exit"
echo "============================================"
read -p "select menu? (1-9) " SELECT_NO

case "$SELECT_NO" in
  "1" ) sudo fastboot boot $BIN_DIR/boot.img ;;
  "2" ) sudo fastboot boot $BIN_DIR/recovery.img ;;
  "3" ) sudo fastboot flash boot $BIN_DIR/boot.img ;;
  "4" ) sudo fastboot flash recovery $BIN_DIR/recovery.img ;;
  "9" ) exit 0 ;;
esac
