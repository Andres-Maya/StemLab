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
                          "Después, IA > Separar instrumentos."_u8,
                          getLocalBounds().reduced (20), juce::Justification::centred, 3);
    }
}

void TrackListView::RecordingLane::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    auto header = bounds.removeFromLeft (TrackView::headerWidth);

    g.setColour (Palette::panel);
    g.fillRect (header);
    g.setColour (Palette::record);
    g.fillRect (header.removeFromLeft (4));
    g.fillEllipse (header.getX() + 12.0f, header.getCentreY() - 6.0f, 12.0f, 12.0f);

    g.setColour (Palette::text);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText ("Grabando...", header.withTrimmedLeft (32), juce::Justification::centredLeft, false);

    g.setColour (Palette::background);
    g.fillRect (bounds);

    if (startSample < 0 || peaks.empty() || bounds.getWidth() <= 0)
        return;

    // Cada pico cubre previewBinSize muestras; se agrupan por columna de píxeles.
    const auto pixelsPerSample = bounds.getWidth() / (timelineLength * sampleRate);
    const auto binSize = static_cast<double> (AudioRecorder::previewBinSize);
    const auto startX = bounds.getX() + static_cast<double> (startSample) * pixelsPerSample;
    const auto endX = startX + static_cast<double> (peaks.size()) * binSize * pixelsPerSample;

    const auto area = bounds.reduced (0, 5).toFloat();
    g.setColour (Palette::record.withAlpha (0.12f));
    g.fillRoundedRectangle (juce::Rectangle<float>::leftTopRightBottom ((float) startX, area.getY(), (float) endX, area.getBottom()), 4.0f);

    g.setColour (Palette::record);
    const auto centreY = area.getCentreY();
    const auto halfHeight = area.getHeight() * 0.5f;
    int column = -1;
    float columnPeak = 0.0f;

    auto drawColumn = [&]
    {
        if (column >= bounds.getX() && column < bounds.getRight() && columnPeak > 0.0f)
        {
            const auto h = juce::jmax (1.0f, juce::jmin (1.0f, columnPeak) * halfHeight);
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
            newRows.push_back (std::move (*existing));
            continue;
        }

        auto row = std::make_unique<TrackView> (track, trackColourFor (track->getName(), static_cast<int> (i)),
                                                engine.getFormatManager(), thumbnailCache);

        row->onSelect = [this] (TrackView& view) { selectTrack (view.getTrackPointer()); };
        row->onSeek = [this] (double seconds) { seekTo (seconds); };
        row->onDelete = [this] (TrackView& view)
        {
            if (onDeleteRequested != nullptr)
                onDeleteRequested (view.getTrack());
        };

        content.addAndMakeVisible (*row);
        newRows.push_back (std::move (row));
    }

    // Las filas de pistas eliminadas se destruyen aquí (y liberan su pista).
    rows = std::move (newRows);
    content.isEmpty = rows.empty() && ! recordingLane.isVisible();
    playhead.toFront (false);

    const auto current = selected.lock();
    const auto stillExists = std::any_of (rows.begin(), rows.end(),
                                          [&] (const auto& row) { return row->getTrackPointer() == current; });

    if (current != nullptr && ! stillExists)
        selectTrack (nullptr);
    else if (current == nullptr && ! rows.empty())
        selectTrack (rows.front()->getTrackPointer());

    knownContentLength = -1;
    updateTimeline();
    layoutRows();
    content.repaint();
}

void TrackListView::selectTrack (const std::shared_ptr<AudioTrack>& track)
{
    const auto previous = selected.lock();
    selected = track;

    for (auto& row : rows)
        row->setSelected (row->getTrackPointer() == track);

    if (previous != track && onSelectionChanged != nullptr)
        onSelectionChanged (track);
}

void TrackListView::resized()
{
    auto bounds = getLocalBounds();
    ruler.setBounds (bounds.removeFromTop (rulerHeight).withTrimmedLeft (TrackView::headerWidth));
    viewport.setBounds (bounds);
    layoutRows();
}

void TrackListView::layoutRows()
{
    const auto width = viewport.getMaximumVisibleWidth();
    const auto numRows = static_cast<int> (rows.size()) + (recordingLane.isVisible() ? 1 : 0);
    const auto height = juce::jmax (viewport.getMaximumVisibleHeight(), numRows * TrackView::preferredHeight);

    content.setSize (width, height);

    int y = 0;

    for (auto& row : rows)
    {
        row->setBounds (0, y, width, TrackView::preferredHeight);
        y += TrackView::preferredHeight;
    }

    // La fila de grabación va debajo de las pistas existentes.
    recordingLane.setBounds (0, y, width, TrackView::preferredHeight);

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
    const auto recording = engine.isRecording();

    if (recording != recordingLane.isVisible())
    {
        recordingLane.peaks.clear();
        recordingLane.startSample = -1;
        recordingLane.setVisible (recording);
        content.isEmpty = rows.empty() && ! recording;
        layoutRows();
        content.repaint();

        // Que la fila de grabación quede a la vista aunque haya muchas pistas.
        if (recording)
            viewport.setViewPosition (0, juce::jmax (0, recordingLane.getBottom() - viewport.getViewHeight()));
    }

    if (! recording)
        return;

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
    // La duración cambia al terminar una grabación o al mover pistas.
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
