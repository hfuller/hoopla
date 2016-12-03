#!/usr/bin/env bash

NAME="$(grep '#define NAME' config.h | cut -d '"' -f 2)"

#terrible build script
~/Downloads/arduino-1.6.7/arduino hoopla.ino
echo    "===================="
read -p "hit return to upload " && curl -vF "image=@hoopla.ino.nodemcu.bin" http://$NAME/update
echo

echo    "==================="
read -p "hit return to reset" && timeout 2 curl -v http://$NAME/debug/reset
