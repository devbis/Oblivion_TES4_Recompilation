#!/bin/sh
# Runs the native desktop build against the game install, in place.
# usage: desktop/run.sh "<game dir>" [save dir]   (LB_LOG=1 for logging)
cd "$(dirname "$0")/.."
GAME=${1:?game dir}
SAVE=${2:-$PWD/save}
mkdir -p "$SAVE"
exec ./desktop/build/lb "$GAME" "$SAVE"
