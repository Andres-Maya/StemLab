<#
.SYNOPSIS
    Compila StemLab, lo prueba y genera el instalador de Windows:
    out\installer\StemLab-Setup.exe

.DESCRIPTION
    El instalador lleva StemLab.exe, los scripts de python\ y un Python
    autónomo (python\runtime) con PyTorch (CPU) y Demucs ya instalados: la
    separación y el MP3 funcionan sin configurar nada.

    Pasos:
      1. Compilar StemLab y StemLabTests (Release) en out\build\installer.
      2. Preparar el Python del instalador: Python 3.12 autónomo
         (python-build-standalone, comprobado con SHA-256) + los paquetes de
         installer\requirements.lock.txt.
      3. Probar: las pruebas rápidas y las de Python (MP3 y separación con
         Demucs) con ese mismo Python. Si algo falla, no hay instalador.
      4. Crear el instalador con Inno Setup (installer\StemLab.iss).

    Necesita Visual Studio con C++ (trae CMake), Git (CMake descarga JUCE) e
    Inno Setup 6 (winget install JRSoftware.InnoSetup). La primera vez
    descarga unos 300 MB (Python y paquetes; se guardan en caché).

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File installer\build-installer.ps1
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'    # Invoke-WebRequest es mucho más rápido sin barra
Set-StrictMode -Version Latest
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $root 'out\installer'
$buildDir = Join-Path $root 'out\build\installer'
$stage = Join-Path $out 'stage'
$cache = Join-Path $out 'cache'

# El mismo Python que el entorno de desarrollo (python\runtime). Para cambiarlo:
# nueva URL y su SHA-256 (los publica python-build-standalone en cada versión).
$pythonArchive = @{
    Url    = 'https://github.com/astral-sh/python-build-standalone/releases/download/20260901/cpython-3.12.14+20260901-x86_64-pc-windows-msvc-install_only_stripped.tar.gz'
    Sha256 = '7c45c9622400d578709a9b2cddbe8124cc21d382409d9f13406d706d28e31b14'
}

function Write-Step([string] $text) {
    Write-Host ''
    Write-Host "== $text" -ForegroundColor Cyan
}

function Invoke-Native([string] $exe, [string[]] $arguments) {
    & $exe @arguments

    if ($LASTEXITCODE -ne 0) {
        throw "$([IO.Path]::GetFileName($exe)) terminó con código $LASTEXITCODE."
    }
}

function Find-CMake {
    $onPath = Get-Command cmake -ErrorAction SilentlyContinue

    if ($onPath) { return $onPath.Source }

    # El que trae Visual Studio (no hace falta la Developer PowerShell).
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'

    if (Test-Path $vswhere) {
        $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath

        if ($vs) {
            $bundled = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
            if (Test-Path $bundled) { return $bundled }
        }
    }

    throw 'No se encontró CMake: instala Visual Studio con la carga de trabajo "Desarrollo para el escritorio con C++".'
}

function Find-InnoSetup {
    $onPath = Get-Command iscc -ErrorAction SilentlyContinue

    if ($onPath) { return $onPath.Source }

    # Instalado para el usuario (winget --scope user) o para todo el equipo.
    $uninstallKeys = @(
        'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1',
        'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1',
        'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1')

    foreach ($key in $uninstallKeys) {
        if (Test-Path $key) {
            $iscc = Join-Path (Get-ItemProperty $key).InstallLocation 'ISCC.exe'
            if (Test-Path $iscc) { return $iscc }
        }
    }

    throw 'No se encontró Inno Setup 6. Instálalo con: winget install JRSoftware.InnoSetup'
}

#------------------------------------------------------------------------------
Write-Step '1/4 Compilar StemLab (Release)'

$cmake = Find-CMake
Invoke-Native $cmake @('-S', $root, '-B', $buildDir, '-A', 'x64')
Invoke-Native $cmake @('--build', $buildDir, '--config', 'Release', '--parallel', '--target', 'StemLab', 'StemLabTests')

$exe = Join-Path $buildDir 'StemLab_artefacts\Release\StemLab.exe'
$tests = Join-Path $buildDir 'Tests\StemLabTests_artefacts\Release\StemLabTests.exe'

#------------------------------------------------------------------------------
Write-Step '2/4 Preparar el Python del instalador'

if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
$pythonFolder = Join-Path $stage 'python'
New-Item -ItemType Directory -Force -Path $pythonFolder, $cache | Out-Null

$archive = Join-Path $cache ([IO.Path]::GetFileName(([Uri] $pythonArchive.Url).LocalPath))

if (-not (Test-Path $archive)) {
    Write-Host "Descargando $($pythonArchive.Url)"
    Invoke-WebRequest -Uri $pythonArchive.Url -OutFile "$archive.partial"
    Move-Item "$archive.partial" $archive
}

$hash = (Get-FileHash -Algorithm SHA256 -Path $archive).Hash

if ($hash -ne $pythonArchive.Sha256) {
    Remove-Item $archive
    throw "El SHA-256 del Python descargado no coincide ($hash). Se ha borrado: vuelve a ejecutar el script."
}

# El archivo trae una carpeta "python": pasa a ser python\runtime.
$extracted = Join-Path $cache 'extracted'
if (Test-Path $extracted) { Remove-Item $extracted -Recurse -Force }
New-Item -ItemType Directory -Path $extracted | Out-Null
Invoke-Native (Join-Path $env:SystemRoot 'System32\tar.exe') @('-xzf', $archive, '-C', $extracted)
Move-Item (Join-Path $extracted 'python') (Join-Path $pythonFolder 'runtime')
Remove-Item $extracted -Recurse -Force

$python = Join-Path $pythonFolder 'runtime\python.exe'
Invoke-Native $python @('-m', 'pip', 'install', '--disable-pip-version-check', '--no-warn-script-location',
                        '-r', (Join-Path $PSScriptRoot 'requirements.lock.txt'))

# Los lanzadores de Scripts\ (demucs.exe, pip.exe...) guardan la ruta de esta
# carpeta temporal y no funcionarían instalados. StemLab no los usa.
Remove-Item (Join-Path $pythonFolder 'runtime\Scripts') -Recurse -Force -ErrorAction SilentlyContinue

foreach ($script in 'stemlab_separate.py', 'stemlab_encode_mp3.py') {
    Copy-Item (Join-Path $root "python\$script") $pythonFolder
}

Invoke-Native $python @('-X', 'utf8', (Join-Path $pythonFolder 'stemlab_separate.py'), '--check')

#------------------------------------------------------------------------------
Write-Step '3/4 Pruebas (rápidas + MP3 y separación con el Python del instalador)'

$env:STEMLAB_PYTHON = $python

try {
    Invoke-Native $tests @('--python', '--output', (Join-Path $out 'test-output'))
}
finally {
    Remove-Item Env:\STEMLAB_PYTHON
}

#------------------------------------------------------------------------------
Write-Step '4/4 Crear el instalador'

Copy-Item $exe $stage

# Con ella, el instalador no deja elegir una carpeta con la que alguna ruta
# pasaría del límite de Windows (MAX_PATH).
$longestPath = (Get-ChildItem $stage -Recurse -File |
    ForEach-Object { $_.FullName.Length - $stage.Length - 1 } | Measure-Object -Maximum).Maximum

$iscc = Find-InnoSetup
Invoke-Native $iscc @('/Qp', "/DStagingDir=$stage", "/DLongestPath=$longestPath", "/O$out", (Join-Path $PSScriptRoot 'StemLab.iss'))

$setup = Get-Item (Join-Path $out 'StemLab-Setup.exe')
$setupHash = (Get-FileHash -Algorithm SHA256 -Path $setup.FullName).Hash
Write-Host ''
Write-Host ('Instalador: {0} ({1:N0} MB)' -f $setup.FullName, ($setup.Length / 1MB)) -ForegroundColor Green
Write-Host "Versión:    $((Get-Item $exe).VersionInfo.ProductVersion)"
Write-Host "SHA-256:    $setupHash"
