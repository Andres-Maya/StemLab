#!/usr/bin/env bash
# Compila StemLab, lo prueba y genera el paquete de Linux:
#   out/linux/StemLab-Linux-x86_64.tar.gz
#
# Es el equivalente de installer/build-installer.ps1 (Windows). El paquete
# lleva el ejecutable, los scripts de python/ y un Python autónomo
# (python/runtime) con PyTorch (CPU) y Demucs ya instalados: la separación y
# el MP3 funcionan sin configurar nada. Se usa descomprimiéndolo y ejecutando
# ./StemLab (install.sh lo añade al menú de aplicaciones).
#
# Pasos:
#   1. Compilar StemLab y StemLabTests (Release) en out/build/linux.
#   2. Preparar el Python del paquete: Python 3.12 autónomo
#      (python-build-standalone, comprobado con SHA-256) + los paquetes de
#      installer/requirements.lock.txt.
#   3. Probar: las pruebas rápidas y las de Python (MP3 y separación con
#      Demucs) con ese mismo Python. Si algo falla, no hay paquete.
#   4. Crear el .tar.gz.
#
# Necesita (Ubuntu / Debian):
#   sudo apt install build-essential cmake pkg-config git curl xvfb openbox \
#       libasound2-dev libjack-jackd2-dev libfreetype-dev libfontconfig1-dev \
#       libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev \
#       libxrandr-dev libxrender-dev libxi-dev libglu1-mesa-dev mesa-common-dev
#
# El ejecutable necesita la glibc del equipo donde se compila o una posterior:
# GitHub Actions lo compila en Ubuntu 24.04 (glibc 2.39).
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="$root/out/linux"
build_dir="$root/out/build/linux"
stage="$out/stage/StemLab"
cache="$out/cache"
package="$out/StemLab-Linux-x86_64.tar.gz"

# El mismo Python que el instalador de Windows. Para cambiarlo: nueva URL y su
# SHA-256 (los publica python-build-standalone en cada versión, en SHA256SUMS).
python_url='https://github.com/astral-sh/python-build-standalone/releases/download/20260901/cpython-3.12.14+20260901-x86_64-unknown-linux-gnu-install_only_stripped.tar.gz'
python_sha256='72748da13197c1fb161e3afeef20a6a385ff24f2165e6e2758e47008e7faba4c'

step() { printf '\n== %s\n' "$1"; }

#------------------------------------------------------------------------------
step '1/4 Compilar StemLab (Release)'

cmake -S "$root" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
# Tantos procesos como núcleos: con Make, --parallel sin número no pone límite
# y compilar todo a la vez agota la memoria (GitHub Actions corta el trabajo).
cmake --build "$build_dir" --parallel "$(nproc)" --target StemLab StemLabTests

exe="$build_dir/StemLab_artefacts/Release/StemLab"
tests="$build_dir/Tests/StemLabTests_artefacts/Release/StemLabTests"

#------------------------------------------------------------------------------
step '2/4 Preparar el Python del paquete'

rm -rf "$out/stage"
mkdir -p "$stage/python" "$cache"

archive="$cache/$(basename "$python_url")"

if [ ! -f "$archive" ]; then
    echo "Descargando $python_url"
    curl --fail --location --silent --show-error --output "$archive.partial" "$python_url"
    mv "$archive.partial" "$archive"
fi

if ! echo "$python_sha256  $archive" | sha256sum --check --status; then
    rm -f "$archive"
    echo 'El SHA-256 del Python descargado no coincide. Se ha borrado: vuelve a ejecutar el script.' >&2
    exit 1
fi

# El archivo trae una carpeta "python": pasa a ser python/runtime.
rm -rf "$cache/extracted"
mkdir -p "$cache/extracted"
tar -xzf "$archive" -C "$cache/extracted"
mv "$cache/extracted/python" "$stage/python/runtime"
rm -rf "$cache/extracted"

python="$stage/python/runtime/bin/python3"
"$python" -m pip install --disable-pip-version-check --no-warn-script-location \
    -r "$root/installer/requirements.lock.txt"

# Los lanzadores de bin/ (demucs, pip...) guardan la ruta de esta carpeta
# temporal y no funcionarían en otro sitio. StemLab no los usa.
find "$stage/python/runtime/bin" -maxdepth 1 -type f ! -name 'python*' -delete

cp "$root/python/stemlab_separate.py" "$root/python/stemlab_encode_mp3.py" "$stage/python/"

"$python" -X utf8 "$stage/python/stemlab_separate.py" --check

#------------------------------------------------------------------------------
step '3/4 Pruebas (rápidas + MP3 y separación con el Python del paquete)'

# Las pruebas crean ventanas: sin pantalla (GitHub Actions) se usa una virtual,
# con un gestor de ventanas (openbox). Sin él, la primera ventana de verdad (un
# aviso) falla con "BadAtom": JUCE usa propiedades que solo existen con uno.
runner=()
if [ -z "${DISPLAY:-}" ]; then
    runner=(xvfb-run --auto-servernum --server-args='-screen 0 1600x1000x24' --error-file="$out/xvfb.log"
            bash -c 'if command -v openbox > /dev/null; then openbox & sleep 2; fi; exec "$@"' --)
fi

# stdbuf: cada línea sale al momento (si las pruebas se cortan, se ve dónde).
run_tests() { STEMLAB_PYTHON="$python" "${runner[@]}" stdbuf -oL -eL "$@"; }

if ! run_tests "$tests" --python --output "$out/test-output"; then
    echo
    echo '== Las pruebas han fallado. Diagnóstico:'
    [ -f "$out/xvfb.log" ] && cat "$out/xvfb.log"

    # Si se cortaron (no es un FALLO de una comprobación), dónde: con gdb, solo las rápidas.
    if command -v gdb > /dev/null; then
        run_tests gdb -batch -ex run -ex bt --args "$tests" --output "$out/test-output" 2>&1 | tail -n 60 || true
    fi

    exit 1
fi

#------------------------------------------------------------------------------
step '4/4 Crear el paquete'

cp "$exe" "$stage/StemLab"
cp "$root/Resources/StemLab.png" "$stage/stemlab.png"
cp "$root/installer/linux/install.sh" "$root/installer/linux/uninstall.sh" "$root/installer/linux/LEEME.txt" "$stage/"
chmod +x "$stage/StemLab" "$stage/install.sh" "$stage/uninstall.sh"

# La versión de project() en CMakeLists.txt (la usa el flujo de publicación).
version="$(sed -n 's/^project(StemLab VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")"
echo "$version" > "$stage/VERSION"

rm -f "$package"
tar -czf "$package" -C "$out/stage" StemLab

echo
echo "Paquete:  $package ($(du -h "$package" | cut -f1))"
echo "Versión:  $version"
echo "SHA-256:  $(sha256sum "$package" | cut -d' ' -f1)"
