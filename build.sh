#!/usr/bin/env bash

#terrible build script
~/Downloads/arduino-1.6.7/arduino hoopla.ino
echo    "===================="
read -p "hit return to upload " && curl -vF "image=@hoopla.ino.nodemcu.bin" http://jennifers-hoop/update
echo

echo    "==================="
read -p "hit return to reset" && timeout 2 curl -v http://jennifers-hoop/debug/reset
