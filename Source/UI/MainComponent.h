#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "AI/AIProcessManager.h"
#include "Audio/AudioEngine.h"
#include "MixerView.h"
#include "Project/ProjectManager.h"
#include "StatusBar.h"
#include "TrackListView.h"
#include "TransportBar.h"

#include <optional>

namespace stemlab
{
/**
    Ventana principal tipo DAW:

        MENÚ        Archivo | Editar | Proyecto | Audio | IA | Ayuda
        TRANSPORTE  ⏮ ▶ ⏹ ⏺ · tiempo · BPM · master
        PISTAS      [cabecera][forma de onda] × N
        MEZCLADOR   canal + efectos de la pista seleccionada
        ESTADO      mensajes · progreso de la IA

    Traduce las acciones del usuario en llamadas a los servicios (motor,
    proyecto, IA). No contiene lógica de audio.
*/
class MainComponent final : public juce::Component,
                            public juce::MenuBarModel,
                            public juce::FileDragAndDropTarget,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    /** Cerrar StemLab: si hay cambios sin guardar pregunta antes. */
    void requestQuit();

    MainComponent (AudioEngine& engine, ProjectManager& projects, AIProcessManager& ai);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void parentHierarchyChanged() override;

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected (int menuItemID, int topLevelMenuIndex) override;

    // FileDragAndDropTarget
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    // Comandos
    void newProject();
    void openProject();
    void saveProject();
    void saveProjectAs (std::function<void()> onSaved = nullptr);
    void importAudio();
    void exportMix();
    void deleteSelectedTrack();
    void removeTrack (AudioTrack& track);
    /** insertIndex < 0: debajo de la pista seleccionada (o al final si no hay). */
    void addTrack (int insertIndex = -1);

    /** Justo debajo de la pista seleccionada, o -1 (al final) si no hay ninguna. */
    int indexBelowSelectedTrack() const;

    // Portapapeles: Ctrl+C / Ctrl+X actúan sobre el fragmento seleccionado o,
    // si no hay (clic en la cabecera), sobre la pista entera. Ctrl+V pega lo
    // último que se copió.
    void copySelection();
    void cutSelection();
    void paste();
    void copyTrack (const std::shared_ptr<AudioTrack>& track);
    void cutTrack (const std::shared_ptr<AudioTrack>& track);
    void pasteTrack (int insertIndex = -1);

    // Edición de fragmentos (clips)
    void undo();
    void redo();
    void splitAtPlayhead();
    void copySelectedClip();
    void cutSelectedClip();
    void pasteClip (std::shared_ptr<AudioTrack> track, juce::int64 position);
    void deleteSelectedClip();
    void showClipMenu (std::shared_ptr<AudioTrack> track, juce::uint32 clipId, double seconds);
    void showProjectFolder();
    void showAudioSettings();
    void togglePlayPause();
    void stop();
    void toggleRecording();
    void finishRecording();
    void separateInstruments();
    void separationFinished (const SeparationResult& result, std::shared_ptr<AudioTrack> source);
    void showAbout();

    bool ensureIdle (const juce::String& action);
    /** Si hay cambios sin guardar pregunta Guardar / No guardar / Cancelar, y
        solo continúa si no se cancela (y, al guardar, si se guardó bien). */
    void askToSaveChanges (std::function<void()> continueAction);
    void saveThen (std::function<void()> action);
    bool unsavedChangesDialogOpen = false;
    ProjectManager::Callback resultHandler (const juce::String& successMessage);
    void reportResult (const juce::Result& result, const juce::String& successMessage);
    void showError (const juce::String& title, const juce::String& message);
    void updateWindowTitle();
    void timerCallback() override   { updateWindowTitle(); }   // marca * de cambios sin guardar

    AudioEngine& engine;
    ProjectManager& projects;
    AIProcessManager& ai;

    juce::MenuBarComponent menuBar;
    juce::TooltipWindow tooltipWindow { this, 600 };   // sin ella no se muestra ningún tooltip
    TransportBar transportBar;
    TrackListView trackList;
    MixerView mixer;
    StatusBar statusBar;

    std::unique_ptr<juce::FileChooser> fileChooser;

    std::optional<AudioClip> clipboard;             // fragmento copiado o cortado
    std::shared_ptr<AudioTrack> trackClipboard;     // copia de la pista copiada o cortada
    std::weak_ptr<AudioTrack> recordingTarget;      // pista donde va la grabación en curso

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
}
