# StemLab

Mini-DAW de escritorio para **cargar una canción, separarla en instrumentos con IA,
editar cada pista, grabar nuevas pistas, aplicar efectos y mezclar**.

- **C++20 + JUCE 9**: interfaz, motor de audio en tiempo real, DSP, proyectos.
- **Python + PyTorch + Demucs**: solo la separación de fuentes, como proceso externo.

```
Archivo/Grabación → Gain → Saturación → EQ → Compresor → Limiter → Mixer → Salida
```

---

## Arquitectura

```
Source/
  Application/  Main.cpp            arranque; crea y destruye los servicios en orden
  Audio/        AudioEngine         callback del dispositivo (hilo de audio)
                Transport           cabezal único para todas las pistas
                AudioMixer          lista de pistas + bus master
                AudioTrack          audio en memoria + vol/pan/mute/solo + efectos
                AudioRecorder       entrada → WAV 24 bits sin bloquear el audio
                AudioFileLoader     decodifica + remuestrea (hilo de trabajo)
  DSP/          AudioEffect         interfaz común de los efectos
                Gain · Saturation · Equalizer · Compressor · Limiter · EffectChain
  AI/           AudioSeparator      interfaz de cualquier motor de separación
                DemucsSeparator     lanza python/stemlab_separate.py
                AIProcessManager    ejecuta la separación en un hilo propio
  Project/      Project · ProjectSerializer (project.json) · ProjectManager
  UI/           MainWindow · MainComponent · TransportBar · TrackListView · TrackView
                WaveformView · TimeRuler · MixerView · EffectPanel · StatusBar ...
  Utils/        Parameter (valor atómico UI ↔ audio) · Strings
python/
  stemlab_separate.py               Demucs: carga, preprocesado, inferencia, stems
```

### Reglas de hilos (lo más importante del diseño)

| Hilo | Qué hace | Qué **no** hace nunca |
|---|---|---|
| **Audio** (`AudioEngine::audioDeviceIOCallbackWithContext`) | mezclar, aplicar efectos, copiar la entrada al FIFO de grabación | reservar memoria, bloquear un lock, leer/escribir disco, liberar objetos |
| **Mensajes** (UI) | interfaz, crear/quitar pistas, guardar `project.json` | decodificar audio largo, ejecutar la IA |
| **Loader** (`ProjectManager`) | decodificar y remuestrear archivos | tocar la UI o la lista de pistas |
| **IA** (`AIProcessManager`) | lanzar Python y leer su progreso | tocar el motor de audio |
| **Grabación** (`ThreadedWriter`) | volcar el FIFO al WAV | — |

Cómo se cumple:

- **Parámetros**: `Parameter` guarda el valor en un `std::atomic<float>`. La UI escribe y el audio lee sin locks.
- **Lista de pistas**: la protege un `CriticalSection`, pero el hilo de audio solo usa `ScopedTryLock`. Si la UI la está modificando justo en ese instante, ese bloque sale en silencio en lugar de bloquear el audio.
- **Vida de las pistas**: son `shared_ptr` y el hilo de audio nunca copia uno, así que una pista nunca se destruye en ese hilo.
- **Cabezal único**: todas las pistas leen de la misma posición (`Transport`), así que los stems están alineados a nivel de muestra. Los saltos se piden con `pendingSeek` y los aplica el hilo de audio.
- **Audio en memoria**: cada pista se decodifica completa (estéreo y a la frecuencia del dispositivo) en el hilo loader. El hilo de audio solo copia muestras. Si cambia la frecuencia del dispositivo, las pistas se recargan solas.

### Comunicación C++ ↔ Python

C++ ejecuta:

```
python -u -X utf8 python/stemlab_separate.py --input X --output stems/X --model htdemucs
```

y lee por stdout líneas con este protocolo:

```
@@STATUS Cargando modelo htdemucs en cuda...
@@PROGRESS 0.4210
@@STEM vocals<TAB>C:\...\stems\cancion\vocals.wav
@@DONE
@@ERROR <mensaje>            (y código de salida != 0)
```

Al terminar, C++ carga los `*.wav` de la carpeta de salida como pistas nuevas y silencia la pista original. Para cambiar de modelo basta con implementar otra `AudioSeparator` (otro script, ONNX Runtime en C++…), sin tocar la UI ni el motor.

### Formato de proyecto

```
MiProyecto/
  project.json      nombre, BPM, master y, por pista: archivo (ruta relativa), inicio,
                    volumen, paneo, mute, solo y parámetros de cada efecto
  audio/            copias de los archivos importados
  stems/            resultados de la IA
  recordings/       grabaciones
  exports/          (reservado para la exportación)
```

Hasta el primer **Guardar como**, la sesión vive en `%TEMP%\StemLab\Sesion-...`.

---

## Compilar (Windows)

Requisitos:

1. **Visual Studio 2022 o posterior** con la carga de trabajo *Desarrollo para el escritorio con C++*, que ya incluye CMake y Ninja.
2. **Git**, necesario para que CMake descargue JUCE 9.0.2 la primera vez (unos 150 MB).

En Visual Studio: **Archivo → Abrir → Carpeta…**, elige la carpeta del repositorio, selecciona el preset `x64 Debug` y ejecuta `StemLab.exe`.

Desde la *Developer PowerShell for VS*:

```powershell
cmake --preset x64-debug
cmake --build --preset x64-debug
.\out\build\x64-debug\StemLab_artefacts\Debug\StemLab.exe
```

Si ya tienes JUCE descargado: `cmake --preset x64-debug -DSTEMLAB_JUCE_DIR=C:/ruta/a/JUCE`.

## Entorno de IA (Python)

Usa **Python 3.11 o 3.12**: algunas dependencias de Demucs pueden no tener todavía paquetes para versiones más nuevas.

```powershell
# Desde la raíz del repositorio
py -3.12 -m venv python\.venv
#   (con Python instalado mediante uv: py -V:Astral/CPython3.12.14 -m venv python\.venv)
python\.venv\Scripts\python -m pip install --upgrade pip

# PyTorch: versión CPU. Para GPU NVIDIA, usa el comando de https://pytorch.org/get-started/locally/
python\.venv\Scripts\python -m pip install torch torchaudio --index-url https://download.pytorch.org/whl/cpu

python\.venv\Scripts\python -m pip install -r python\requirements.txt
python\.venv\Scripts\python python\stemlab_separate.py --check
```

> **Importante:** crea el entorno con un Python que exista de verdad en tu disco (por ejemplo,
> uno de python.org). Un entorno virtual solo guarda la ruta del Python con el que se creó; si ese
> Python desaparece o se instaló dentro de otra aplicación empaquetada (como la app de escritorio de
> Claude, que virtualiza `AppData\Roaming`), StemLab mostrará *"Python terminó con código 103: No
> Python at …"*. Alternativa portátil: copiar un Python autónomo (p. ej. los de `uv`) a
> `python/runtime/` (ignorada por Git) y apuntar `home` de `python/.venv/pyvenv.cfg` a esa carpeta.

StemLab busca el intérprete en este orden:

1. La variable de entorno `STEMLAB_PYTHON`.
2. `python/.venv/Scripts/python.exe`: primero junto al ejecutable y después en el repositorio.
3. `python` del `PATH`.

La primera separación descarga el modelo, unos 80 MB para `htdemucs`. **FFmpeg** es opcional: solo se usa para formatos que libsndfile no lee, como m4a o aac.

## Uso

1. **Archivo → Importar audio…**, o arrastra un archivo a la ventana.
2. **IA → Separar instrumentos**. El modelo se elige en el mismo menú:
   - `htdemucs`: 4 pistas.
   - `htdemucs_ft`: 4 pistas, más calidad y más lento.
   - `htdemucs_6s`: 6 pistas, añade guitarra y piano.
3. Cada pista tiene Mute, Solo, volumen y paneo. Al seleccionarla, el mezclador muestra su canal y su cadena de efectos.
4. ⏺ graba una pista nueva desde la entrada elegida en **Audio → Configuración de audio**. La grabación se compensa por la latencia del dispositivo.
5. **Archivo → Guardar proyecto como…**

Atajos de teclado:

| Tecla | Acción |
|---|---|
| Espacio | reproducir / pausa |
| Inicio | ir al principio |
| R | grabar |
| Supr | eliminar la pista seleccionada |
| Ctrl+N / O / S / I | nuevo / abrir / guardar / importar |
| Ctrl+Shift+S | guardar como |

---

## Limitaciones conocidas y hoja de ruta

- **Memoria**: el audio se guarda en memoria como float estéreo, unos 23 MB por minuto y pista a 48 kHz. Para canciones muy largas conviene leer del disco en streaming (`BufferingAudioSource`).
- **Saturación sin sobremuestreo**: con drive alto aparece aliasing. Añadir `juce::dsp::Oversampling` introduce latencia, así que antes hace falta compensar la latencia entre pistas.
- **Pendiente**:
  - Exportar la mezcla a WAV con un render offline en `exports/`.
  - `SpectrogramView` con FFT.
  - Zoom y desplazamiento de la línea de tiempo.
  - Mover clips.
  - Deshacer/rehacer con `UndoManager`.
  - Reverb, Delay, Pitch Shift y Time Stretching.
  - Aviso de cambios sin guardar al salir.
  - Pruebas unitarias del DSP y del serializador.
