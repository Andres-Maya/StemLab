#include "ExportDialog.h"

#include "Utils/Strings.h"

#include <memory>

namespace stemlab
{
namespace
{
    struct FormatOption
    {
        const char* label;
        ExportFormat format;
        int mp3Bitrate;
    };

    const FormatOption formatOptions[] {
        // Los nombres se traducen al mostrarlos (ver la lista "Formato").
        { "WAV 24 bits (recomendado)",      ExportFormat::wav24,      0 },     // msg ("WAV 24 bits (recomendado)")
        { "WAV 16 bits (calidad CD)",       ExportFormat::wav16,      0 },     // msg ("WAV 16 bits (calidad CD)")
        { "WAV 32 bits coma flotante",      ExportFormat::wav32Float, 0 },     // msg ("WAV 32 bits coma flotante")
        { "MP3 320 kbps",                   ExportFormat::mp3,        320 },
        { "MP3 192 kbps",                   ExportFormat::mp3,        192 },
        { "MP3 128 kbps",                   ExportFormat::mp3,        128 },
    };

    /** Render en segundo plano con la ventana de progreso de JUCE. Se borra
        sola al terminar. */
    class ExportJob final : public juce::ThreadWithProgressWindow
    {
    public:
        ExportJob (std::unique_ptr<MixExporter> mixExporter, ExportSettings exportSettings,
                   juce::Component* parent, ExportDialog::Callback callback)
            : ThreadWithProgressWindow (tr ("Exportar mezcla"), true, true, 10000, tr ("Cancelar"), parent),
              exporter (std::move (mixExporter)),
              settings (std::move (exportSettings)),
              onDone (std::move (callback))
        {
            setStatusMessage (tr ("Exportando {0}...", settings.file.getFileName()));
        }

        void run() override
        {
            result = exporter->render (settings, [this] (double value)
            {
                setProgress (value);
                return ! threadShouldExit();
            });
        }

        void threadComplete (bool userPressedCancel) override
        {
            if (userPressedCancel)
            {
                result.cancelled = true;
                result.status = juce::Result::fail (tr ("Exportación cancelada."));
            }

            if (onDone != nullptr)
                onDone (result, settings.file);

            delete this;
        }

    private:
        std::unique_ptr<MixExporter> exporter;
        ExportSettings settings;
        ExportDialog::Callback onDone;
        ExportResult result;
    };

    void chooseFileAndExport (juce::Component* parent, AudioEngine& engine, std::unique_ptr<juce::FileChooser>& chooser,
                              const FormatOption& option, const juce::File& defaultFolder,
                              const juce::String& defaultName, ExportDialog::Callback onDone)
    {
        const auto extension = MixExporter::getFileExtension (option.format);
        defaultFolder.createDirectory();

        chooser = std::make_unique<juce::FileChooser> (tr ("Exportar mezcla como"),
                                                       defaultFolder.getChildFile (defaultName + extension),
                                                       "*" + extension);

        chooser->launchAsync (juce::FileBrowserComponent::saveMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [parent = juce::Component::SafePointer<juce::Component> (parent), &engine,
                               option, extension, onDone] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();

            if (file == juce::File() || parent == nullptr)
                return;

            if (! file.hasFileExtension (extension))
                file = file.withFileExtension (extension);

            ExportSettings settings;
            settings.file = file;
            settings.format = option.format;
            settings.mp3Bitrate = option.mp3Bitrate;

            // La copia de la mezcla se hace aquí, en el hilo de mensajes.
            auto exporter = std::make_unique<MixExporter> (engine.getMixer(), engine.getSampleRate());
            auto* job = new ExportJob (std::move (exporter), std::move (settings), parent.getComponent(), onDone);
            job->launchThread();
        });
    }
}

void ExportDialog::show (juce::Component* parent, AudioEngine& engine, std::unique_ptr<juce::FileChooser>& chooserOwner,
                         const juce::File& defaultFolder, const juce::String& defaultName, Callback onDone)
{
    auto* window = new juce::AlertWindow (tr ("Exportar mezcla"),
                                          tr ("Se exporta la mezcla completa tal como suena: volumen, paneo, mute, "
                                              "solo, efectos y volumen master."),
                                          juce::MessageBoxIconType::NoIcon, parent);

    juce::StringArray labels;

    for (const auto& option : formatOptions)
        labels.add (tr (option.label));

    window->addComboBox ("format", labels, tr ("Formato"));
    window->addButton (tr ("Exportar..."), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (tr ("Cancelar"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create (
        [window, parent = juce::Component::SafePointer<juce::Component> (parent), &engine, &chooserOwner,
         defaultFolder, defaultName, onDone = std::move (onDone)] (int result)
    {
        // JUCE llama a esta función antes de borrar la ventana: aún se puede leer.
        const auto index = window->getComboBoxComponent ("format")->getSelectedItemIndex();

        if (result == 0 || parent == nullptr || ! juce::isPositiveAndBelow (index, (int) std::size (formatOptions)))
            return;

        chooseFileAndExport (parent.getComponent(), engine, chooserOwner, formatOptions[index],
                             defaultFolder, defaultName, onDone);
    }), true);
}
}
