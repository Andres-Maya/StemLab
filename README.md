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
                FileAssociation     .stemlab con el icono de StemLab y doble clic (Windows)
  Audio/        AudioEngine         callback del dispositivo (hilo de audio)
                Transport           cabezal único para todas las pistas
                AudioMixer          lista de pistas + bus master
                AudioTrack          audio en memoria + vol/pan/mute/solo + efectos
                AudioRecorder       entrada → WAV 24 bits sin bloquear el audio
                AudioFileLoader     decodifica + remuestrea (hilo de trabajo)
                MixExporter         render offline de la mezcla a WAV / MP3
  DSP/          AudioEffect         interfaz común de los efectos
                Gain · Saturation · Equalizer · Compressor · Limiter · EffectChain
  AI/           AudioSeparator      interfaz de cualquier motor de separación
                DemucsSeparator     lanza python/stemlab_separate.py
                AIProcessManager    ejecuta la separación en un hilo propio
  Project/      Project · ProjectSerializer (archivo .stemlab) · ProjectManager (+ deshacer/rehacer)
  UI/           MainWindow · MainComponent · TransportBar · TrackListView · TrackView
                WaveformView · TimeRuler · MixerView · EffectPanel · ExportDialog ...
  Utils/        Parameter (valor atómico UI ↔ audio) · PythonEnvironment · Strings
Resources/
  StemLab.png · StemLabSmall.png    icono de la aplicación (logo completo; solo el emblema en 16-48 px)
  StemLab.ico                       icono de los archivos .stemlab (16 a 256 px)
python/
  stemlab_separate.py               Demucs: carga, preprocesado, inferencia, stems
  stemlab_encode_mp3.py             codifica a MP3 la mezcla exportada (LAME)
Tests/                              pruebas automáticas (ver "Pruebas")
```

### Reglas de hilos (lo más importante del diseño)

| Hilo | Qué hace | Qué **no** hace nunca |
|---|---|---|
| **Audio** (`AudioEngine::audioDeviceIOCallbackWithContext`) | mezclar, aplicar efectos, copiar la entrada al FIFO de grabación | reservar memoria, bloquear un lock, leer/escribir disco, liberar objetos |
| **Mensajes** (UI) | interfaz, crear/quitar pistas, guardar el `.stemlab` | decodificar audio largo, ejecutar la IA |
| **Loader** (`ProjectManager`) | decodificar y remuestrear archivos | tocar la UI o la lista de pistas |
| **IA** (`AIProcessManager`) | lanzar Python y leer su progreso | tocar el motor de audio |
| **Exportación** (`ExportDialog`) | renderizar una copia de la mezcla y escribir el archivo | tocar el motor de audio |
| **Grabación** (`ThreadedWriter`) | volcar el FIFO al WAV | — |

Cómo se cumple:

- **Parámetros**: `Parameter` guarda el valor en un `std::atomic<float>`. La UI escribe y el audio lee sin locks.
- **Lista de pistas**: la protege un `CriticalSection`, pero el hilo de audio solo usa `ScopedTryLock`. Si la UI la está modificando justo en ese instante, ese bloque sale en silencio en lugar de bloquear el audio.
- **Vida de las pistas**: son `shared_ptr` y el hilo de audio nunca copia uno, así que una pista nunca se destruye en ese hilo.
- **Cabezal único**: todas las pistas leen de la misma posición (`Transport`), así que los stems están alineados a nivel de muestra. Los saltos se piden con `pendingSeek` y los aplica el hilo de audio.
- **Audio en memoria**: cada pista se decodifica completa (estéreo y a la frecuencia del dispositivo) en el hilo loader. El hilo de audio solo copia muestras. Si cambia la frecuencia del dispositivo, las pistas se recargan solas.
- **Exportación**: `MixExporter` copia las pistas (clips, volumen, paneo, mute, solo, efectos y master) en el hilo de mensajes y renderiza esa copia en su propio hilo. Nunca comparte estado con el hilo de audio, así que se puede seguir escuchando mientras exporta. El audio de los clips se comparte sin copiar porque es de solo lectura.
- **Deshacer/rehacer**: `ProjectManager` guarda en un `juce::UndoManager` la lista de clips de antes y de después de cada edición. Los clips comparten el audio, así que cada paso ocupa muy poco.

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
  MiProyecto.stemlab  el proyecto: un JSON (versión 2) con nombre, BPM, master y, por
                    pista: sus clips (archivo relativo, inicio, desde dónde del archivo
                    y duración, en segundos), volumen, paneo, mute, solo y parámetros
                    de cada efecto. Los proyectos de la versión 1 se siguen abriendo.
  audio/            copias de los archivos importados
  stems/            resultados de la IA
  recordings/       grabaciones
  exports/          mezclas exportadas (WAV / MP3)
```

Hasta el primer **Guardar como**, la sesión vive en `%TEMP%\StemLab\Sesion-...`.

Las rutas son relativas a la carpeta, así que el proyecto se puede mover o copiar entero (por ejemplo, a otro equipo). Los proyectos antiguos guardaban el mismo JSON en `project.json`: se siguen abriendo (eligiendo el `project.json` o su carpeta), **Guardar** los mantiene así y **Guardar como** en su misma carpeta los convierte en `.stemlab`.

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

El mismo entorno sirve para **exportar a MP3**: JUCE solo sabe leer MP3, así que StemLab escribe un WAV temporal y lo codifica `python/stemlab_encode_mp3.py` con LAME (paquete `lameenc`, incluido en `requirements.txt`). Exportar a WAV no necesita Python.

## Uso

1. **Archivo → Importar audio…**, o arrastra un archivo a la ventana.
2. **IA → Separar instrumentos**. El modelo se elige en el mismo menú:
   - `htdemucs`: 4 pistas.
   - `htdemucs_ft`: 4 pistas, más calidad y más lento.
   - `htdemucs_6s`: 6 pistas, añade guitarra y piano.
3. Cada pista tiene Mute, Solo, volumen y paneo. Al seleccionarla, el mezclador muestra su canal y su cadena de efectos.
4. **Grabar:** selecciona una pista (clic en ella) y pulsa el botón rojo ⏺ del transporte o **R**. Si no hay ninguna pista seleccionada, se crea una nueva. Mientras graba, la franja de la pista se pone roja.
   - La grabación empieza en el cabezal. En una pista los fragmentos **nunca se solapan**: si el cabezal está sobre audio ya grabado, la toma empieza justo después de ese audio. Si al grabar llega al fragmento siguiente, se corta ahí (el audio sigue en el archivo; puedes alargar la toma arrastrando su borde si mueves antes el otro fragmento). Con **Ctrl+Z** se quita la toma.
   - Para grabar **encima** de algo ya grabado, usa otra pista: con **+** o **Ctrl+T** creas una pista vacía justo debajo, ya seleccionada.
   - Para grabar aparte de la canción, primero crea una pista con el botón **+** (queda seleccionada).
   - Mientras se graba, el cabezal no se puede mover: la toma ocupa un tramo continuo desde donde empezó.
   - Pausar (**R** o Espacio) y volver a pulsar **R** sigue grabando **en la misma pista**, como un fragmento nuevo justo después del anterior.
   - La entrada se elige en **Audio → Configuración de audio**, y la grabación se compensa por la latencia del dispositivo.
   - **Entrada** (barra superior): ganancia del micrófono antes de grabar, +18 dB por defecto, con su medidor. El medidor se mueve aunque no grabes, para ajustar el nivel antes. Un limitador suave evita el recorte brusco.
   - En Windows se graba en **modo RAW**, sin la supresión de ruido ni el control automático de ganancia del sistema o del controlador. Esos efectos atenuaban o silenciaban los sonidos constantes. JUCE no pide este modo, así que `cmake/PatchJuceRawCapture.cmake` aplica un pequeño parche a la copia de JUCE descargada. Si el micrófono no admite RAW, se graba como antes.
   - En ese mismo diálogo, **Usar la salida predeterminada de Windows** (activada por defecto) hace que StemLab cambie solo a los audífonos al conectarlos. Si eliges otra salida a mano, la opción se desactiva.
5. **Editar fragmentos** (clips). La edición no destructiva nunca modifica los archivos de audio:
   - **Clic** en un fragmento lo selecciona y coloca el cabezal en ese punto. **Arrastrar el centro** lo desplaza. **Arrastrar un borde** lo recorta.
   - **S** divide en el cabezal. **Ctrl+X / Ctrl+C / Ctrl+V** cortan, copian y pegan en el cabezal. Si el cabezal está sobre audio, lo pegado va justo después, en el primer hueco donde quepa. **Supr** o **Retroceso** eliminan el fragmento seleccionado.
   - **Clic derecho** abre el menú de edición.
   - **Mover delante o detrás de otro fragmento:** al arrastrar, el fragmento se detiene al tocar a su vecino. Si sigues arrastrando hasta que su centro pase del centro del vecino, salta al otro lado. Si ahí no cabe, se abre espacio: los fragmentos que quedan por delante se desplazan todos juntos lo justo, sin cambiar las distancias entre ellos. Si vuelves atrás sin soltar, regresan a su sitio. Todo el arrastre se deshace con un solo **Ctrl+Z**.
   - Mientras lo arrastras, el fragmento **se levanta**: sube un poco, con sombra y un halo, y sigue al ratón por encima de los demás, que se oscurecen debajo. Los que se apartan se deslizan, y al soltar baja hasta su sitio.
   - Al recortar, el borde se detiene al llegar al fragmento vecino. Nunca queda un fragmento encima de otro.
   - **Deshacer / rehacer:** **Ctrl+Z** deshace y **Ctrl+Y** (o **Ctrl+Shift+Z**) rehace. También están en el menú **Editar**, que muestra qué se va a deshacer. Se puede deshacer todo lo que se hace con fragmentos (dividir, cortar, pegar, eliminar, mover, recortar, grabar) y con pistas (añadir, pegar, cortar, eliminar, mover, cambiar el nombre, importar audio y separar instrumentos). El historial se vacía al crear o abrir un proyecto.
6. **Pistas:**
   - **Añadir:** al pasar el ratón por una pista aparece un **+** en un círculo, centrado sobre su borde inferior en la esquina derecha de la cabecera. Añade una pista justo debajo. Si no hay pistas, el **+** está arriba del todo.
   - **Mover:** arrastra la cabecera (nombre o zona vacía) arriba o abajo, o usa **Alt+↑ / Alt+↓**.
   - **Cambiar el nombre:** doble clic en el nombre, **F2**, o clic derecho en la cabecera.
   - **Eliminar:** la **×** de cada pista, o clic en su cabecera y **Supr** / **Retroceso**, tras pedir confirmación. **Ctrl+Z** la recupera con sus fragmentos, volumen y efectos.
   - **Copiar, cortar y pegar pistas:** haz clic en la cabecera de la pista (así no queda ningún fragmento seleccionado) y pulsa **Ctrl+C** o **Ctrl+X**. **Ctrl+V** pega una copia debajo de la pista seleccionada, con sus fragmentos, volumen, paneo y efectos. Si el nombre ya existe, se añade "(copia)". También está en el menú **Editar** y en el clic derecho de la cabecera.
   - Con un fragmento seleccionado, **Ctrl+C / Ctrl+X** actúan sobre el fragmento. **Ctrl+V** pega siempre lo último que copiaste, sea un fragmento o una pista.
7. **Zoom y desplazamiento:**
   - **Ctrl + rueda** acerca o aleja alrededor del ratón.
   - **Shift + rueda**, o la rueda horizontal del touchpad, desplaza a los lados. También sirve la barra inferior.
   - También en **Proyecto → Vista**.
   - Durante la reproducción, la vista sigue al cabezal.
8. **Archivo → Exportar mezcla…** (**Ctrl+E**) guarda la canción completa tal como suena: volumen, paneo, mute, solo, efectos y master.
   - Formatos: **WAV** de 24 bits (recomendado), 16 bits o 32 bits en coma flotante, y **MP3** a 320, 192 o 128 kbps. El MP3 necesita el entorno de Python.
   - Se exporta desde el principio hasta el final del último fragmento. Por defecto se guarda en la carpeta `exports/` del proyecto. Si el proyecto aún no se ha guardado, se guarda en `Documentos\StemLab`.
   - La ventana de progreso tiene **Cancelar**. Si cancelas, no queda ningún archivo a medias.
   - Si la mezcla pasa de 0 dBFS, StemLab avisa de que se ha recortado. El WAV de 32 bits en coma flotante no recorta.
9. **Guardar y abrir proyectos:**
   - **Archivo → Guardar proyecto como…** (**Ctrl+Shift+S**): escribe un nombre, por ejemplo `MiCancion`. StemLab crea la carpeta `MiCancion/` con `MiCancion.stemlab` y el audio del proyecto dentro. Por defecto se guarda en `Documentos\StemLab`. Después, **Ctrl+S** guarda en el mismo archivo.
   - **Archivo → Abrir proyecto…** (**Ctrl+O**): elige un `.stemlab`.
   - **Archivo → Abrir reciente**: los últimos 10 proyectos abiertos o guardados. La lista se conserva al cerrar StemLab.
   - **Doble clic en un `.stemlab`** en el Explorador lo abre en StemLab. Los archivos llevan el icono de StemLab: StemLab lo copia a `AppData\Local\StemLab\Icons`, con un nombre que cambia si cambia el icono, para que Windows no muestre uno antiguo guardado en caché. Al arrancar, StemLab asocia la extensión con su ejecutable solo para tu usuario (`HKEY_CURRENT_USER\Software\Classes`, sin permisos de administrador). Si mueves `StemLab.exe`, se actualiza en el siguiente arranque. Para quitar la asociación, borra en esa rama del registro las claves `.stemlab` y `StemLab.Project`.
   - También puedes **arrastrar un `.stemlab` a la ventana**. Si StemLab ya está abierto, el proyecto se abre en esa ventana.
   - Con cambios sin guardar, el título de la ventana muestra **\***. Al cerrar StemLab, crear un proyecto nuevo o abrir otro, pregunta **Guardar / No guardar / Cancelar**. Si el proyecto nunca se guardó, "Guardar" abre "Guardar como".

Atajos de teclado:

| Tecla | Acción |
|---|---|
| Espacio | reproducir / pausa |
| Inicio | ir al principio |
| R | grabar / pausar la grabación (en la pista seleccionada) |
| S | dividir el fragmento en el cabezal |
| Ctrl+Z | deshacer |
| Ctrl+Y / Ctrl+Shift+Z | rehacer |
| Ctrl+X / C / V | cortar / copiar / pegar el fragmento seleccionado, o la pista entera si no hay fragmento seleccionado |
| Supr / Retroceso | eliminar el fragmento seleccionado, o la pista si no hay fragmento seleccionado (pide confirmación) |
| Ctrl+T | añadir pista (debajo de la seleccionada) |
| F2 | cambiar el nombre de la pista seleccionada |
| Alt+↑ / Alt+↓ | subir / bajar la pista seleccionada |
| Ctrl+Supr | eliminar la pista seleccionada |
| Ctrl+N / O / S / I | nuevo / abrir / guardar / importar |
| Ctrl+Shift+S | guardar como |
| Ctrl+E | exportar la mezcla (WAV / MP3) |

---

## Pruebas

Las pruebas automáticas están en `Tests/`. Son un ejecutable de consola, `StemLabTests`, que compila el mismo código que la aplicación. Se compilan con el proyecto, salvo que configures con `-DSTEMLAB_BUILD_TESTS=OFF`.

```powershell
cmake --build --preset debug --target StemLabTests
ctest --test-dir out\build\vs2026 -C Debug                          # pruebas rápidas
.\out\build\vs2026\Tests\StemLabTests_artefacts\Debug\StemLabTests.exe --all
```

| Opción | Qué prueba | Necesita |
|---|---|---|
| *(ninguna)* | DSP, mezclador, clips, carga y grabador, proyectos, cambios sin guardar, deshacer/rehacer (fragmentos y pistas), copiar y pegar pistas con el teclado, fragmentos sin solaparse (grabar, pegar, mover por delante de otro abriendo espacio, recortar), animación al arrastrar, Supr / Retroceso, clic sobre un fragmento, exportar a WAV e interfaz sin audio | nada (tarda segundos; es lo que ejecuta `ctest`) |
| `--device` | grabar de verdad (incluida una toma con el cabezal sobre audio, que va a continuación), recuperar el dispositivo, seguir la salida de Windows | tarjeta de sonido y micrófono |
| `--python` | exportar a MP3 y separar con Demucs (suma de stems, cancelar, errores) | `python/.venv` (ver arriba); tarda ~1 min en CPU |
| `--all` | todo lo anterior | lo anterior |
| `--acoustic` | reproduce ruido por los altavoces y comprueba que el micrófono no lo atenúa (modo RAW) | altavoces y micrófono; hace ruido |
| `--output <carpeta>` | dónde se dejan WAV, MP3, proyectos y capturas PNG | por defecto `test-output/` junto al ejecutable |

Cada comprobación imprime `ok:` o `FALLO:`. Al final aparece `RESULTADO: n/m`, y el código de salida es 0 solo si todo pasó. Las capturas de la interfaz (`addrow-*.png`, `lane*.png`, `clips.png`) sirven para revisarla a ojo.

---

## Limitaciones conocidas y hoja de ruta

- **Memoria**: el audio se guarda en memoria como float estéreo, unos 23 MB por minuto y pista a 48 kHz. Para canciones muy largas conviene leer del disco en streaming (`BufferingAudioSource`).
- **Saturación sin sobremuestreo**: con drive alto aparece aliasing. Añadir `juce::dsp::Oversampling` introduce latencia, así que antes hace falta compensar la latencia entre pistas.
- **Deshacer/rehacer** cubre los fragmentos y las pistas. Los cambios de volumen, paneo, mute, solo y efectos todavía no se deshacen. Al deshacer una separación desaparecen los stems, pero la pista original sigue silenciada.
- **Pendiente**:
  - Exportar solo un tramo (entre marcadores) o cada pista por separado (stems).
  - `SpectrogramView` con FFT.
  - Arrastrar fragmentos entre pistas y ajuste a la rejilla de compases (BPM).
  - Reverb, Delay, Pitch Shift y Time Stretching.
