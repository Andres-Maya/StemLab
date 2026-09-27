# -----------------------------------------------------------------------------
# Parche de StemLab para JUCE: grabar en modo RAW en Windows (WASAPI).
#
# Por defecto Windows aplica a los micrófonos los efectos del controlador
# (supresión de ruido, control automático de ganancia, etc.). Con un sonido
# constante, la supresión de ruido lo va atenuando hasta silenciarlo, y el
# control de ganancia cambia el volumen de la toma. Las aplicaciones de
# grabación piden el modo RAW (AUDCLNT_STREAMOPTIONS_RAW), que se salta esos
# efectos, pero JUCE no lo hace. Este script añade esa petición al crear el
# cliente de audio de los dispositivos de ENTRADA. Si el dispositivo no admite
# RAW, Windows lo ignora y se graba como antes.
#
# Se aplica sobre la copia de JUCE descargada por FetchContent (nunca sobre una
# copia local indicada con STEMLAB_JUCE_DIR) y es idempotente.
# -----------------------------------------------------------------------------

function(stemlab_patch_juce_raw_capture juce_source_dir)
    set(target "${juce_source_dir}/modules/juce_audio_devices/native/juce_WASAPI_windows.cpp")

    if(NOT EXISTS "${target}")
        message(WARNING "StemLab: no se encontró ${target}; se graba sin modo RAW.")
        return()
    endif()

    file(READ "${target}" content)

    string(FIND "${content}" "[StemLab] RAW" alreadyPatched)
    if(NOT alreadyPatched EQUAL -1)
        return()
    endif()

    set(anchor [==[        return newClient;]==])

    set(replacement [==[        // [StemLab] RAW: en los micrófonos, pedir el flujo sin los efectos de
        // Windows/controlador (supresión de ruido, control automático de ganancia).
        if (newClient != nullptr && getDataFlow (device) == eCapture)
        {
            if (auto client2 = newClient.getInterface<IAudioClient2>())
            {
                struct { UINT32 cbSize; BOOL bIsOffload; AUDIO_STREAM_CATEGORY eCategory; UINT32 options; } properties
                    { sizeof (properties), FALSE, AudioCategory_Other, 1 /* AUDCLNT_STREAMOPTIONS_RAW */ };

                (void) client2->SetClientProperties (reinterpret_cast<const AudioClientProperties*> (&properties));
            }
        }

        return newClient;]==])

    string(FIND "${content}" "${anchor}" anchorPosition)
    if(anchorPosition EQUAL -1)
        message(WARNING "StemLab: esta versión de JUCE no coincide con el parche de captura RAW; se graba sin modo RAW.")
        return()
    endif()

    string(REPLACE "${anchor}" "${replacement}" content "${content}")
    file(WRITE "${target}" "${content}")
    message(STATUS "StemLab: parche de captura RAW aplicado a JUCE")
endfunction()
