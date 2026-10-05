#pragma once

// Grupos de pruebas. TestMain.cpp elige cuáles se ejecutan.

namespace stemlab::test
{
/** DSP, mezclador, clips, carga, grabador, proyectos y qué Python se usa (UnitTests.cpp). */
void runUnitTests();

/** Deshacer/rehacer, grabar encima de audio y clic sobre un clip (EditingTests.cpp). */
void runEditingTests();

/** Pistas: deshacer añadir/eliminar/mover/renombrar/importar y copiar/pegar
    pistas, también con las teclas de la ventana (TrackTests.cpp). */
void runTrackTests();

/** Carpetas de separación: meter/sacar pistas, lista desplegable, ventana de ondas
    y la carpeta del proyecto en disco al guardar (FolderTests.cpp). */
void runFolderTests();

/** Archivos .stemlab: guardar, abrir, proyectos antiguos y "Abrir reciente" (ProjectFileTests.cpp). */
void runProjectFileTests();

/** Exportar la mezcla a WAV (ExportTests.cpp). */
void runExportTests();

/** Interfaz sin audio: + de pistas, reordenar, zoom (UiTests.cpp). */
void runUiTests();

/** Idiomas (todos los textos traducidos), temas y tutorial (InterfaceTests.cpp). */
void runInterfaceTests();

/** Tarjeta de sonido real: grabar, recuperar el dispositivo, salida de Windows (DeviceTests.cpp). */
void runDeviceTests();

/** Reproduce ruido por los altavoces y lo graba con el micrófono (DeviceTests.cpp). */
void runAcousticTest();

/** Python: exportar a MP3 y separar con Demucs (PythonTests.cpp). */
void runPythonTests();
}
