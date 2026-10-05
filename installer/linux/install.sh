#!/usr/bin/env bash
# Añade StemLab al menú de aplicaciones del usuario y asocia los proyectos
# .stemlab (sin permisos de administrador). StemLab se queda en esta carpeta:
# si la mueves, vuelve a ejecutar este script.
#
# Adds StemLab to your applications menu and associates .stemlab projects (no
# root needed). StemLab stays in this folder: run this script again if you
# move it.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
data="${XDG_DATA_HOME:-$HOME/.local/share}"

mkdir -p "$data/applications" "$data/icons/hicolor/256x256/apps" "$data/mime/packages"
cp "$here/stemlab.png" "$data/icons/hicolor/256x256/apps/stemlab.png"

cat > "$data/applications/stemlab.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=StemLab
Comment=Separate, record, edit and mix instruments
Comment[es]=Separa, graba, edita y mezcla instrumentos
Exec="$here/StemLab" %f
Icon=stemlab
Terminal=false
Categories=AudioVideo;Audio;
MimeType=application/x-stemlab-project;
StartupWMClass=StemLab
EOF

cat > "$data/mime/packages/stemlab.xml" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<mime-info xmlns="http://www.freedesktop.org/standards/shared-mime-info">
  <mime-type type="application/x-stemlab-project">
    <comment>StemLab project</comment>
    <comment xml:lang="es">Proyecto de StemLab</comment>
    <glob pattern="*.stemlab"/>
    <icon name="stemlab"/>
  </mime-type>
</mime-info>
EOF

# Si faltan estas herramientas, el menú se actualiza al volver a iniciar sesión.
command -v update-mime-database > /dev/null && update-mime-database "$data/mime" || true
command -v update-desktop-database > /dev/null && update-desktop-database "$data/applications" || true
command -v gtk-update-icon-cache > /dev/null && gtk-update-icon-cache -q -t "$data/icons/hicolor" || true

echo "StemLab está en el menú de aplicaciones / StemLab is in your applications menu."
