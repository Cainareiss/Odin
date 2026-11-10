#!/bin/sh
# Usage: mkpatch.sh <absolute out.patch> [file ...]    (see mkpatch.py: the snapshot core/state advances)
set -e
exec C:/Python314/python.exe -I C:/claude-work/odinuwp/overlay/mkpatch.py "$@"
