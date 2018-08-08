#!/usr/bin/env bash

NAME="$1"
PLATFORM="$2"

#terrible build script
~/Downloads/arduino-1.*/arduino hoopla.ino
echo    "===================="
read -p "hit return to upload " && 
curl -v#F "image=@hoopla.ino.$PLATFORM.bin" http://$NAME/update
echo

#echo    "==================="
#read -p "hit return to reset" && timeout 2 curl -v http://$NAME/debug/reset
