#!/usr/bin/env bash
set -e

PREFIX="/usr/local"
BINDIR="$PREFIX/bin"

# Build
make all

# Install
install -d "$BINDIR"
install -m 755 winscope "$BINDIR/"

echo "WinScope wurde nach $BINDIR installiert"