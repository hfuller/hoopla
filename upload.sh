#!/usr/bin/env bash

NAME="$1"
PLATFORM="$2"

curl -v#F "image=@hoopla.ino.$PLATFORM.bin" http://$NAME/update
