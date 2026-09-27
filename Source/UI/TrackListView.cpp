#include "TrackListView.h"

#include "Utils/Strings.h"

#include <iterator>

namespace stemlab
{
namespace
{
    constexpr int rulerHeight = 24;
}

void TrackListView::Content::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background);

    if (isEmpty)
    {
        g.setColour (Palette::textDim);
        g.setFont (juce::FontOptions (16.0f));
        g.drawFittedText ("Arrastra aquí una canción o usa Archivo > Importar audio...\n"
                          "Después, IA > Separar instrumentos. Para grabar, pulsa R."_u8,
                          getLocalBounds().reduced (20), juce::Justification::centred, 3);
    }
}

void TrackListView::RecordingLane::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds();

    if (! startSample.has_value() || peaks.empty() || bounds.getWidth() <= 0)
        return;

    // Cada pico cubre previewBinSize muestras; se agrupan por columna de píxeles.
    // Si el clip empieza antes del 0, lo que queda a la izquierda no se dibuja
    // (igual que el clip final, que recorta ese trozo).
    const auto pixelsPerSample = bounds.getWidth() / (timelineLength * sampleRate);
    const auto binSize = static_cast<double> (AudioRecorder::previewBinSize);
    const auto startX = static_cast<double> (*startSample) * pixelsPerSample;
    const auto endX = startX + static_cast<double> (peaks.size()) * binSize * pixelsPerSample;

    // Se graba "encima" de lo que hubiera en la pista: se tapa esa zona.
    const auto area = bounds.reduced (0, 3).toFloat();
    const auto region = juce::Rectangle<float>::leftTopRightBottom (juce::jmax (0.0f, (float) startX), area.getY(),
                                                                    (float) endX, area.getBottom());
    g.setColour (Palette::background);
    g.fillRect (region);
    g.setColour (Palette::record.withAlpha (0.15f));
    g.fillRoundedRectangle (region, 4.0f);

    g.setColour (Palette::record);
    const auto centreY = area.getCentreY();
    const auto halfHeight = area.getHeight() * 0.5f - 2.0f;
    int column = -1;
    float columnPeak = 0.0f;

    auto drawColumn = [&]
    {
        if (column >= 0 && column < bounds.getRight() && columnPeak > 0.0f)
        {
            // Escala en dB (-60..0), como un medidor de grabación: los micrófonos
            // integrados captan bajo y en escala lineal apenas se verían.
            const auto db = juce::Decibels::gainToDecibels (columnPeak, -60.0f);
            const auto h = juce::jmax (1.0f, juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f) * halfHeight);
            g.fillRect ((float) column, centreY - h, 1.0f, h * 2.0f);
        }
    };

    for (size_t i = 0; i < peaks.size(); ++i)
    {
        const auto x = static_cast<int> (startX + static_cast<double> (i) * binSize * pixelsPerSample);

        if (x != column)
        {
            drawColumn();
            column = x;
            columnPeak = 0.0f;
        }

        columnPeak = juce::jmax (columnPeak, peaks[i]);
    }

    drawColumn();
}

void TrackListView::Playhead::paint (juce::Graphics& g)
{
    if (x >= 0)
    {
        g.setColour (Palette::accent);
        g.fillRect (x, 0, 2, getHeight());
    }
}

//==============================================================================
TrackListView::TrackListView (AudioEngine& audioEngine)
    : engine (audioEngine)
{
    addTrackButton.setTooltip ("Añadir una pista vacía (Ctrl+T)"_u8);
    addTrackButton.onClick = [this] { if (onAddTrack != nullptr) onAddTrack(); };
    addAndMakeVisible (addTrackButton);

    removeTrackButton.setTooltip ("Eliminar la pista seleccionada");
    removeTrackButton.onClick = [this] { if (onRemoveTrack != nullptr) onRemoveTrack(); };
    addAndMakeVisible (removeTrackButton);

    ruler.onSeek = [this] (double seconds) { seekTo (seconds); };
    addAndMakeVisible (ruler);

    content.addChildComponent (recordingLane);
    content.addAndMakeVisible (playhead);
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    startTimerHz (30);
}

TrackListView::~TrackListView()
{
    stopTimer();
}

void TrackListView::refresh()
{
    const auto& tracks = engine.getMixer().getTracks();
    std::vector<std::unique_ptr<TrackView>> newRows;

    for (size_t i = 0; i < tracks.size(); ++i)
    {
        const auto& track = tracks[i];
        const auto existing = std::find_if (rows.begin(), rows.end(), [&] (const auto& row)
        {
            return row != nullptr && row->getTrackPointer() == track;
        });

        if (existing != rows.end())
        {
            (*existing)->trackChanged();
            newRows.push_back (std::move (*existing));
            continue;
        }

        auto row = std::make_unique<TrackView> (track, trackColourFor (track->getName(), static_cast<int> (i)),
                                                engine.getFormatManager(), thumbnailCache);

        row->onSelect = [this] (TrackView& view) { selectTrack (view.getTrackPointer()); };
        row->onSeek = [this] (double seconds) { seekTo (seconds); };
        row->onDelete = [this] (TrackView& view) { if (onDeleteRequested != nullptr) onDeleteRequested (view.getTrack()); };
        row->onClipClicked = [this] (TrackView& view, juce::uint32 clipId) { selectClip (view.getTrackPointer(), clipId); };
        row->onClipsEdited = [this] { if (onClipsEdited != nullptr) onClipsEdited(); };
        row->onContextMenu = [this] (TrackView& view, juce::uint32 clipId, double seconds)
        {
            if (onContextMenu != nullptr)
                onContextMenu (view.getTrackPointer(), clipId, seconds);
        };

        content.addAndMakeVisible (*row);
        newRows.push_back (std::move (row));
    }

    // Las filas de pistas eliminadas se destruyen aquí (y liberan su pista).
    rows = std::move (newRows);
    content.isEmpty = rows.empty();
    recordingLane.toFront (false);
    playhead.toFront (false);

    const auto current = selected.lock();
    const auto stillExists = std::any_of (rows.begin(), rows.end(),
                                          [&] (const auto& row) { return row->getTrackPointer() == current; });

    if (current != nullptr && ! stillExists)
        selectTrack (nullptr);
    else if (current == nullptr && ! rows.empty())
        selectTrack (rows.front()->getTrackPointer());

    // El clip seleccionado puede haber desaparecido (eliminado o cortado).
    if (auto track = selected.lock(); track != nullptr && selectedClip != 0)
    {
        auto clips = track->getClips();

        if (ClipEditing::find (clips, selectedClip) == nullptr)
            selectedClip = 0;
    }

    updateSelectionDisplay();
    knownContentLength = -1;
    updateTimeline();
    layoutRows();
    content.repaint();
}

void TrackListView::selectTrack (const std::shared_ptr<AudioTrack>& track)
{
    const auto previous = selected.lock();
    selected = track;

    if (previous != track)
    {
        selectedClip = 0;

        if (onSelectionChanged != nullptr)
            onSelectionChanged (track);
    }

    updateSelectionDisplay();
}

void TrackListView::selectClip (const std::shared_ptr<AudioTrack>& track, juce::uint32 clipId)
{
    selectTrack (track);
    selectedClip = clipId;
    updateSelectionDisplay();
}

void TrackListView::updateSelectionDisplay()
{
    const auto current = selected.lock();

    for (auto& row : rows)
    {
        const auto isSelected = row->getTrackPointer() == current;
        row->setSelected (isSelected);
        row->setSelectedClip (isSelected ? selectedClip : 0);
    }
}

void TrackListView::paint (juce::Graphics& g)
{
    g.setColour (Palette::panel);
    g.fillRect (getLocalBounds().removeFromTop (rulerHeight).withWidth (TrackView::headerWidth));
}

void TrackListView::resized()
{
    auto bounds = getLocalBounds();
    auto top = bounds.removeFromTop (rulerHeight);

    auto buttons = top.removeFromLeft (TrackView::headerWidth).reduced (6, 2);
    addTrackButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).withTrimmedRight (3));
    removeTrackButton.setBounds (buttons.withTrimmedLeft (3));

    ruler.setBounds (top);
    viewport.setBounds (bounds);
    layoutRows();
}

void TrackListView::layoutRows()
{
    const auto width = viewport.getMaximumVisibleWidth();
    const auto height = juce::jmax (viewport.getMaximumVisibleHeight(),
                                    static_cast<int> (rows.size()) * TrackView::preferredHeight);

    content.setSize (width, height);

    int y = 0;

    for (auto& row : rows)
    {
        row->setBounds (0, y, width, TrackView::preferredHeight);
        y += TrackView::preferredHeight;
    }

    playhead.setBounds (TrackView::headerWidth, 0, juce::jmax (0, width - TrackView::headerWidth), height);

    // La regla debe medir lo mismo que la zona de formas de onda.
    ruler.setBounds (ruler.getBounds().withWidth (playhead.getWidth()));
}

void TrackListView::updateTimeline()
{
    const auto sampleRate = engine.getSampleRate();
    const auto contentSeconds = static_cast<double> (engine.getMixer().getContentLength()) / sampleRate;

    // Un poco de margen a la derecha para poder grabar después del final.
    setTimelineLength (juce::jmax (30.0, contentSeconds * 1.05));
}

void TrackListView::setTimelineLength (double seconds)
{
    timelineLength = seconds;
    ruler.setTimelineLength (seconds);
    recordingLane.timelineLength = seconds;
    recordingLane.repaint();

    for (auto& row : rows)
        row->setTimelineLength (seconds);
}

void TrackListView::updateRecordingLane()
{
    // La vista previa se dibuja sobre la zona de clips de la pista armada.
    TrackView* targetRow = nullptr;

    if (engine.isRecording())
        for (auto& row : rows)
            if (row->getTrack().isArmed())
                targetRow = row.get();

    if (targetRow == nullptr)
    {
        if (recordingLane.isVisible())
        {
            recordingLane.setVisible (false);
            recordingLane.peaks.clear();
            recordingLane.startSample.reset();
        }

        return;
    }

    const auto laneBounds = targetRow->getBounds().withTrimmedLeft (TrackView::headerWidth).withTrimmedBottom (1);

    if (! recordingLane.isVisible())
    {
        recordingLane.peaks.clear();
        recordingLane.startSample.reset();
        recordingLane.setVisible (true);

        // Que la pista que se graba quede a la vista aunque haya muchas.
        viewport.setViewPosition (0, juce::jmax (0, laneBounds.getBottom() - viewport.getViewHeight()));
    }

    if (recordingLane.getBounds() != laneBounds)
        recordingLane.setBounds (laneBounds);

    float buffer[256];
    auto added = false;

    for (int n; (n = engine.readRecordingPeaks (buffer, (int) std::size (buffer))) > 0;)
    {
        recordingLane.peaks.insert (recordingLane.peaks.end(), buffer, buffer + n);
        added = true;
    }

    recordingLane.startSample = engine.getRecordingClipStart();
    recordingLane.sampleRate = engine.getSampleRate();

    if (added)
        recordingLane.repaint();
}

void TrackListView::seekTo (double seconds)
{
    engine.getTransport().setPosition (static_cast<juce::int64> (seconds * engine.getSampleRate()));
}

void TrackListView::timerCallback()
{
    // La duración cambia al terminar una grabación o al editar clips.
    if (const auto length = engine.getMixer().getContentLength(); length != knownContentLength)
    {
        knownContentLength = length;
        updateTimeline();
    }

    updateRecordingLane();

    const auto seconds = static_cast<double> (engine.getTransport().getPosition()) / engine.getSampleRate();

    // Grabando más allá del final: la línea de tiempo se alarga por delante del cabezal.
    if (engine.isRecording() && seconds > timelineLength - 2.0)
        setTimelineLength (seconds * 1.25);

    ruler.setPlayheadSeconds (seconds);

    const auto x = playhead.getWidth() > 0
                 ? juce::roundToInt (seconds / timelineLength * playhead.getWidth())
                 : -1;

    if (x != playhead.x)
    {
        // Solo se repintan las dos franjas afectadas: las formas de onda están
        // cacheadas como imagen.
        playhead.repaint (playhead.x - 2, 0, 5, playhead.getHeight());
        playhead.x = x;
        playhead.repaint (x - 2, 0, 5, playhead.getHeight());
    }
}
}
