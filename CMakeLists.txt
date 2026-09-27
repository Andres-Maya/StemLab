cmake_minimum_required(VERSION 3.25)

project(StemLab VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ----------------------------------------------------------------------------
# JUCE
# Por defecto se descarga automáticamente la versión fijada. Si ya tienes una
# copia local: cmake --preset x64-debug -DSTEMLAB_JUCE_DIR=C:/ruta/a/JUCE
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
endif()

# ----------------------------------------------------------------------------
# Aplicación
# ----------------------------------------------------------------------------
juce_add_gui_app(StemLab
    PRODUCT_NAME "StemLab"
    COMPANY_NAME "StemLab")

target_sources(StemLab PRIVATE
    Source/Application/Main.cpp

    Source/Audio/AudioEngine.cpp
    Source/Audio/AudioFileLoader.cpp
    Source/Audio/AudioMixer.cpp
    Source/Audio/AudioRecorder.cpp
    Source/Audio/AudioTrack.cpp

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
    Source/UI/IconButton.cpp
    Source/UI/LevelMeter.cpp
    Source/UI/MainComponent.cpp
    Source/UI/MainWindow.cpp
    Source/UI/MixerView.cpp
    Source/UI/ParameterAttachments.cpp
    Source/UI/StatusBar.cpp
    Source/UI/StemLabLookAndFeel.cpp
    Source/UI/TimeRuler.cpp
    Source/UI/TrackListView.cpp
    Source/UI/TrackView.cpp
    Source/UI/TransportBar.cpp
    Source/UI/WaveformView.cpp

    Source/Utils/Parameter.cpp)

target_include_directories(StemLab PRIVATE Source)

target_compile_definitions(StemLab PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_WINDOWS_MEDIA_FORMAT=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:StemLab,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:StemLab,JUCE_VERSION>"
    # Carpeta del módulo de IA en desarrollo (en una instalación se busca
    # primero una carpeta "python" junto al ejecutable).
    "STEMLAB_PYTHON_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/python\"")

if(MSVC)
    # Los textos de la interfaz llevan acentos: el código fuente es UTF-8.
    target_compile_options(StemLab PRIVATE /utf-8)
endif()

target_link_libraries(StemLab
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
