#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Audio/AudioEngine.h"
#include "Audio/MixExporter.h"

#include <functional>

namespace stemlab
{
/**
    Archivo > Exportar mezcla:

        1. Diálogo con el formato (WAV 16/24/32 bits, MP3 128/192/320 kbps).
        2. "Guardar como" (por defecto en exports/ del proyecto).
        3. Render en un hilo de trabajo con barra de progreso y Cancelar.

    La mezcla se copia al elegir el archivo (ver MixExporter): lo que se
    edite mientras se exporta no afecta al archivo.
*/
namespace ExportDialog
{
    using Callback = std::function<void (const ExportResult&, const juce::File&)>;

    /** Hilo de mensajes. defaultFolder y defaultName proponen dónde guardar.
        chooserOwner guarda el diálogo de archivo mientras está abierto (debe
        vivir más que el diálogo, como el FileChooser de MainComponent). */
    void show (juce::Component* parent, AudioEngine& engine, std::unique_ptr<juce::FileChooser>& chooserOwner,
               const juce::File& defaultFolder, const juce::String& defaultName, Callback onDone);
}
}
