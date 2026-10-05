cmake_minimum_required(VERSION 3.25)

project(StemLab VERSION 0.2.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Runtime de C++ estático (MSVC): StemLab.exe no depende de las DLL del
# "Visual C++ Redistributable", así funciona en un Windows recién instalado
# (el instalador solo lleva el ejecutable). Antes de JUCE y de los targets.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ----------------------------------------------------------------------------
# JUCE
# Por defecto se descarga automáticamente la versión fijada. Si ya tienes una
# copia local: cmake --preset vs2026 -DSTEMLAB_JUCE_DIR=C:/ruta/a/JUCE
# ----------------------------------------------------------------------------
set(STEMLAB_JUCE_DIR "" CACHE PATH "Ruta a una copia local de JUCE (opcional)")

if(STEMLAB_JUCE_DIR)
    add_subdirectory("${STEMLAB_JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        9.0.2
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE)
    FetchContent_MakeAvailable(JUCE)

    # Grabar sin la supresión de ruido / control de ganancia de Windows (modo RAW).
    include(cmake/PatchJuceRawCapture.cmake)
    stemlab_patch_juce_raw_capture("${juce_SOURCE_DIR}")
endif()

# ----------------------------------------------------------------------------
# Código de StemLab (todo menos el punto de entrada). Lo compilan la aplicación
# y las pruebas automáticas (Tests/).
# ----------------------------------------------------------------------------
set(STEMLAB_SOURCES
    Source/Application/FileAssociation.cpp

    Source/Audio/AudioClip.cpp
    Source/Audio/AudioEngine.cpp
    Source/Audio/AudioFileLoader.cpp
    Source/Audio/AudioMixer.cpp
    Source/Audio/AudioRecorder.cpp
    Source/Audio/AudioTrack.cpp
    Source/Audio/MixExporter.cpp

    Source/DSP/AudioEffect.cpp
    Source/DSP/CompressorEffect.cpp
    Source/DSP/EffectChain.cpp
    Source/DSP/EqualizerEffect.cpp
    Source/DSP/GainEffect.cpp
    Source/DSP/LimiterEffect.cpp
    Source/DSP/SaturationEffect.cpp

    Source/AI/AIProcessManager.cpp
    Source/AI/AudioSeparator.cpp
    Source/AI/DemucsSeparator.cpp

    Source/Project/Project.cpp
    Source/Project/ProjectManager.cpp
    Source/Project/ProjectSerializer.cpp

    Source/UI/AudioSettingsComponent.cpp
    Source/UI/EffectPanel.cpp
    Source/UI/ExportDialog.cpp
    Source/UI/IconButton.cpp
    Source/UI/LevelMeter.cpp
    Source/UI/MainComponent.cpp
    Source/UI/MainWindow.cpp
    Source/UI/MixerView.cpp
    Source/UI/ParameterAttachments.cpp
    Source/UI/SeparationWindow.cpp
    Source/UI/StatusBar.cpp
    Source/UI/StemLabLookAndFeel.cpp
    Source/UI/TimeRuler.cpp
    Source/UI/TourOverlay.cpp
    Source/UI/TrackListView.cpp
    Source/UI/TrackView.cpp
    Source/UI/TransportBar.cpp
    Source/UI/WaveformView.cpp

    Source/Utils/Localisation.cpp
    Source/Utils/Parameter.cpp
    Source/Utils/PythonEnvironment.cpp
    Source/Utils/Translations_en.cpp)

# Rutas absolutas: Tests/CMakeLists.txt usa la misma lista.
set(STEMLAB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
list(TRANSFORM STEMLAB_SOURCES PREPEND "${STEMLAB_ROOT}/")

# Opciones comunes de la aplicación y de las pruebas.
function(stemlab_configure_target target)
    target_include_directories(${target} PRIVATE "${STEMLAB_ROOT}/Source")

    target_compile_definitions(${target} PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_USE_MP3AUDIOFORMAT=1
        JUCE_USE_WINDOWS_MEDIA_FORMAT=1
        # Carpeta de los scripts de Python en desarrollo (en una instalación se
        # busca primero una carpeta "python" junto al ejecutable).
        "STEMLAB_PYTHON_DIR=\"${STEMLAB_ROOT}/python\"")

    if(MSVC)
        # Los textos de la interfaz llevan acentos: el código fuente es UTF-8.
        target_compile_options(${target} PRIVATE /utf-8)
    endif()

    target_link_libraries(${target}
        PRIVATE
            StemLabBinaryData
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endfunction()

# ----------------------------------------------------------------------------
# Aplicación
# ----------------------------------------------------------------------------
# Icono de la aplicación (ventana, barra de tareas y StemLab.exe): JUCE usa
# ICON_SMALL (48 px) para 16, 32 y 48 px e ICON_BIG (256 px) para 256 px; con
# esos tamaños exactos no tiene que ampliar. El .ico se genera al configurar:
# si cambian las imágenes, hay que volver a ejecutar cmake --preset.
juce_add_gui_app(StemLab
    PRODUCT_NAME "StemLab"
    COMPANY_NAME "StemLab"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/StemLab.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Resources/StemLabSmall.png")

# Icono de los archivos .stemlab (todos los tamaños de 16 a 256 px). Va dentro
# del ejecutable; al arrancar, StemLab lo escribe en AppData\Local y lo asocia.
juce_add_binary_data(StemLabBinaryData
    HEADER_NAME StemLabBinaryData.h
    NAMESPACE StemLabBinaryData
    SOURCES Resources/StemLab.ico)

target_sources(StemLab PRIVATE Source/Application/Main.cpp ${STEMLAB_SOURCES})
stemlab_configure_target(StemLab)

target_compile_definitions(StemLab PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:StemLab,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:StemLab,JUCE_VERSION>")

# ----------------------------------------------------------------------------
# Pruebas automáticas (Tests/): cmake --build ... --target StemLabTests
# y después ctest, o el ejecutable directamente (ver README.md, "Pruebas").
# ----------------------------------------------------------------------------
option(STEMLAB_BUILD_TESTS "Compilar las pruebas automáticas (Tests/)" ON)

if(STEMLAB_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()
