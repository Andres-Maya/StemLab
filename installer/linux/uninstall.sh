#!/usr/bin/env bash
# Quita StemLab del menú de aplicaciones y la asociación de los .stemlab (lo
# que añadió install.sh). Después puedes borrar esta carpeta. Los ajustes y
# los proyectos se quedan.
#
# Removes StemLab from the applications menu and the .stemlab association
# (what install.sh added). You can then delete this folder. Your settings and
# projects are kept.
set -euo pipefail

data="${XDG_DATA_HOME:-$HOME/.local/share}"

rm -f "$data/applications/stemlab.desktop" \
      "$data/icons/hicolor/256x256/apps/stemlab.png" \
      "$data/mime/packages/stemlab.xml"

command -v update-mime-database > /dev/null && update-mime-database "$data/mime" || true
command -v update-desktop-database > /dev/null && update-desktop-database "$data/applications" || true

echo "StemLab ya no está en el menú de aplicaciones / StemLab was removed from your applications menu."
