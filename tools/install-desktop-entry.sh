#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 rc4l
#
# [rc4l] Puts ForkUnderA in the desktop's application menu with its icon, pointing at the folder this script sits in.
# [rc4l] linux_compile.sh ships it beside the binary; run it again after moving that folder.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
DATA="${XDG_DATA_HOME:-$HOME/.local/share}"
mkdir -p "$DATA/applications" "$DATA/icons/hicolor/512x512/apps"
cp "$HERE/forkundera.png" "$DATA/icons/hicolor/512x512/apps/forkundera.png"
cat > "$DATA/applications/forkundera.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=ForkUnderA
Exec="$HERE/forkundera"
Path=$HERE
Icon=forkundera
Terminal=false
Categories=Game;ActionGame;
StartupWMClass=forkundera
EOF
echo "installed $DATA/applications/forkundera.desktop"
