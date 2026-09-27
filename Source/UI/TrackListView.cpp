#include "TrackListView.h"

#include "Utils/Strings.h"

#include <cmath>
#include <iterator>

namespace stemlab
{
namespace
{
    constexpr int rulerHeight = 24;
    constexpr int scrollBarHeight = 12;
    constexpr double minimumVisibleSeconds = 0.25;     // zoom máximo: 0,25 s a lo ancho
    constexpr double zoomStep = 1.5;
}

void TrackListView::Content::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background);

    if (isEmpty)
    {
        g.setColour (Palette::textDim);
        g.setFont (juce::FontOptions (16.0f));
        g.drawFittedText ("Pulsa + para añadir una pista, arrastra aquí una canción o usa Archivo > Importar audio...\n"
                          "Después, IA > Separar instrumentos. Para grabar, pulsa R."_u8,
                          getLocalBounds().withTrimmedTop (EmptyAddButton::height).reduced (20),
                          juce::Justification::centred, 3);
    }
}

//==============================================================================
bool TrackListView::EmptyAddButton::hitTest (int x, int y)
{
    constexpr float radius = 9.0f;
    const juce::Point<float> centre (6.0f + radius, static_cast<float> (getHeight()) - 1.0f - radius);
    return centre.getDistanceFrom ({ static_cast<float> (x), static_cast<float> (y) }) <= radius + 3.0f;
}

void TrackListView::EmptyAddButton::paint (juce::Graphics& g)
{
    // Mismo estilo que el "+" de cada pista, algo más claro: sin pistas es la
    // única forma de empezar. La línea queda en la zona de cabeceras.
    constexpr float radius = 9.0f;
    const juce::Point<float> centre (6.0f + radius, static_cast<float> (getHeight()) - 1.0f - radius);
    const auto lineY = static_cast<float> (getHeight()) - 2.0f;
    const auto lineColour = Palette::accent.withAlpha (hovered ? 1.0f : 0.8f);

    g.setColour (lineColour);
    g.fillRect (centre.x, lineY, static_cast<float> (getWidth()) - centre.x, 2.0f);

    g.setColour (Palette::panel.interpolatedWith (Palette::accent, hovered ? 0.4f : 0.2f));
    g.fillEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
    g.setColour (lineColour);
    g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.5f);

    g.setColour (Palette::text);
    g.fillRoundedRectangle (centre.x - 4.5f, centre.y - 0.75f, 9.0f, 1.5f, 0.75f);
    g.fillRoundedRectangle (centre.x - 0.75f, centre.y - 4.5f, 1.5f, 9.0f, 0.75f);
}

void TrackListView::EmptyAddButton::mouseUp (const juce::MouseEvent& event)
{
    if (hitTest (event.x, event.y) && onClick != nullptr)
        onClick();
}

//==============================================================================
void TrackListView::RecordingLane::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds();

    if (! startSample.has_value() || peaks.empty() || bounds.getWidth() <= 0)
        return;

    // Cada pico cubre previewBinSize muestras; se agrupan por columna de píxeles.
    // Si el clip empieza antes del 0, lo que queda a la izquierda no se dibuja
    // (igual que el clip final, que recorta ese trozo).
    const auto pixelsPerSample = bounds.getWidth() / (visibleLength * sampleRate);
    const auto binSize = static_cast<double> (AudioRecorder::previewBinSize);
    const auto startX = (static_cast<double> (*startSample) - visibleStart * sampleRate) * pixelsPerSample;
    const auto endX = startX + static_cast<double> (peaks.size()) * binSize * pixelsPerSample;

    // Se graba "encima" de lo que hubiera en la pista: se tapa esa zona.
    const auto area = bounds.reduced (0, 3).toFloat();
    const auto region = juce::Rectangle<float>::leftTopRightBottom (juce::jmax (0.0f, (float) startX), area.getY(),
                                                                    juce::jmin ((float) bounds.getRight(), (float) endX),
                                                                    area.getBottom());
    if (region.getWidth() <= 0.0f)
        return;

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
        const auto x = static_cast<int> (std::floor (startX + static_cast<double> (i) * binSize * pixelsPerSample));

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
    zoomOutButton.setTooltip ("Alejar (Ctrl + rueda del ratón)"_u8);
    zoomInButton.setTooltip ("Acercar (Ctrl + rueda del ratón; Shift + rueda para desplazarte)"_u8);
    zoomFitButton.setTooltip ("Ver toda la canción"_u8);
    zoomOutButton.onClick = [this] { zoomOut(); };
    zoomInButton.onClick = [this] { zoomIn(); };
    zoomFitButton.onClick = [this] { zoomToFit(); };

    for (auto* button : { &zoomOutButton, &zoomInButton, &zoomFitButton })
        addAndMakeVisible (*button);

    horizontalScroll.setAutoHide (false);
    horizontalScroll.setColour (juce::ScrollBar::thumbColourId, Palette::outline.brighter (0.4f));
    horizontalScroll.addListener (this);
    addAndMakeVisible (horizontalScroll);

    emptyAddButton.setTooltip ("Añadir una pista (Ctrl+T)"_u8);
    emptyAddButton.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    emptyAddButton.onClick = [this] { if (onAddTrack != nullptr) onAddTrack (-1); };
    content.addAndMakeVisible (emptyAddButton);

    ruler.onSeek = [this] (double seconds) { seekTo (seconds); };
    ruler.onWheel = [this] (int x, const juce::MouseEvent& e, const juce::MouseWheelDetails& w) { return handleWheel (x, e, w); };
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
    horizontalScroll.removeListener (this);
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
        row->onWheel = [this] (int x, const juce::MouseEvent& e, const juce::MouseWheelDetails& w) { return handleWheel (x, e, w); };
        row->onContextMenu = [this] (TrackView& view, juce::uint32 clipId, double seconds)
        {
            if (onContextMenu != nullptr)
                onContextMenu (view.getTrackPointer(), clipId, seconds);
        };
        row->onAddBelow = [this] (TrackView& view)
        {
            // La pista nueva va justo debajo de la que tiene el "+".
            for (size_t index = 0; index < rows.size(); ++index)
                if (rows[index].get() == &view && onAddTrack != nullptr)
                    onAddTrack (static_cast<int> (index) + 1);
        };

        row->setVisibleRange (visibleStart, visibleLength);
        content.addAndMakeVisible (*row);
        newRows.push_back (std::move (row));
    }

    // Las filas de pistas eliminadas se destruyen aquí (y liberan su pista).
    rows = std::move (newRows);
    content.isEmpty = rows.empty();
    emptyAddButton.setVisible (rows.empty());

    for (size_t i = 0; i < rows.size(); ++i)
        rows[i]->setIsLast (i + 1 == rows.size());

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
    layoutRows();
    updateTimeline();
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

//==============================================================================
void TrackListView::paint (juce::Graphics& g)
{
    g.setColour (Palette::panel);
    g.fillRect (getLocalBounds().removeFromTop (rulerHeight).withWidth (TrackView::headerWidth));
    g.fillRect (getLocalBounds().removeFromBottom (scrollBarHeight));
}

void TrackListView::resized()
{
    auto bounds = getLocalBounds();
    auto top = bounds.removeFromTop (rulerHeight);
    auto bottom = bounds.removeFromBottom (scrollBarHeight);

    // Esquina superior izquierda: controles de zoom.
    auto zoomArea = top.removeFromLeft (TrackView::headerWidth).reduced (6, 2);
    zoomFitButton.setBounds (zoomArea.removeFromRight (64));
    zoomArea.removeFromRight (4);
    zoomInButton.setBounds (zoomArea.removeFromRight (26));
    zoomArea.removeFromRight (4);
    zoomOutButton.setBounds (zoomArea.removeFromRight (26));

    ruler.setBounds (top);
    viewport.setBounds (bounds);
    horizontalScroll.setBounds (bottom.withTrimmedLeft (TrackView::headerWidth));
    layoutRows();
    setVisibleRange (visibleStart, visibleLength);
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

    // Sin pistas, el "+" va arriba del todo, en la zona de cabeceras.
    emptyAddButton.setBounds (0, 0, TrackView::headerWidth, EmptyAddButton::height);

    playhead.setBounds (TrackView::headerWidth, 0, juce::jmax (0, width - TrackView::headerWidth), height);

    // La regla debe medir lo mismo que la zona de formas de onda.
    ruler.setBounds (ruler.getBounds().withWidth (playhead.getWidth()));
}

int TrackListView::waveformWidth() const
{
    return juce::jmax (1, viewport.getMaximumVisibleWidth() - TrackView::headerWidth);
}

//==============================================================================
void TrackListView::updateTimeline()
{
    const auto sampleRate = engine.getSampleRate();
    const auto contentSeconds = static_cast<double> (engine.getMixer().getContentLength()) / sampleRate;

    // Un poco de margen a la derecha para poder grabar después del final.
    totalLength = juce::jmax (30.0, contentSeconds * 1.05);

    if (fitToWindow)
    {
        setVisibleRange (0.0, totalLength);
    }
    else
    {
        totalLength = juce::jmax (totalLength, visibleStart + visibleLength);
        setVisibleRange (visibleStart, visibleLength);
    }
}

void TrackListView::setVisibleRange (double start, double length)
{
    length = juce::jlimit (minimumVisibleSeconds, juce::jmax (minimumVisibleSeconds, totalLength), length);
    start = juce::jlimit (0.0, juce::jmax (0.0, totalLength - length), start);

    visibleStart = start;
    visibleLength = length;

    ruler.setVisibleRange (start, length);
    recordingLane.visibleStart = start;
    recordingLane.visibleLength = length;
    recordingLane.repaint();

    for (auto& row : rows)
        row->setVisibleRange (start, length);

    horizontalScroll.setRangeLimits (0.0, totalLength, juce::dontSendNotification);
    horizontalScroll.setCurrentRange (start, length, juce::dontSendNotification);

    playhead.x = -2;    // fuerza a recolocar el cabezal en el siguiente tic
}

void TrackListView::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
{
    fitToWindow = false;
    setVisibleRange (newRangeStart, visibleLength);
}

void TrackListView::zoomAround (double anchorSeconds, double factor)
{
    const auto newLength = juce::jlimit (minimumVisibleSeconds, totalLength, visibleLength * factor);

    if (newLength >= totalLength - 1.0e-6)
    {
        zoomToFit();
        return;
    }

    // El instante bajo el ratón (o el cabezal) se queda en el mismo sitio.
    const auto newStart = anchorSeconds - (anchorSeconds - visibleStart) * newLength / visibleLength;
    fitToWindow = false;
    setVisibleRange (newStart, newLength);
}

void TrackListView::zoomIn()
{
    const auto playheadSeconds = static_cast<double> (engine.getTransport().getPosition()) / engine.getSampleRate();
    const auto playheadVisible = playheadSeconds >= visibleStart && playheadSeconds <= visibleStart + visibleLength;
    zoomAround (playheadVisible ? playheadSeconds : visibleStart + visibleLength * 0.5, 1.0 / zoomStep);
}

void TrackListView::zoomOut()
{
    zoomAround (visibleStart + visibleLength * 0.5, zoomStep);
}

void TrackListView::zoomToFit()
{
    fitToWindow = true;
    updateTimeline();
}

bool TrackListView::handleWheel (int x, const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    // Ctrl + rueda: zoom alrededor del ratón.
    if (event.mods.isCommandDown() || event.mods.isCtrlDown())
    {
        const auto delta = ! juce::exactlyEqual (wheel.deltaY, 0.0f) ? wheel.deltaY : wheel.deltaX;

        if (! juce::exactlyEqual (delta, 0.0f))
        {
            const auto anchor = visibleStart + static_cast<double> (x) / waveformWidth() * visibleLength;
            zoomAround (anchor, std::pow (2.0, -static_cast<double> (delta) * 2.0));
        }

        return true;
    }

    // Shift + rueda, o rueda horizontal del touchpad: desplazamiento lateral.
    const auto horizontal = event.mods.isShiftDown()
                          ? (! juce::exactlyEqual (wheel.deltaX, 0.0f) ? wheel.deltaX : wheel.deltaY)
                          : (std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : 0.0f);

    if (! juce::exactlyEqual (horizontal, 0.0f))
    {
        fitToWindow = false;
        setVisibleRange (visibleStart - static_cast<double> (horizontal) * visibleLength * 0.5, visibleLength);
        return true;
    }

    return false;   // rueda normal: desplazamiento vertical de la lista
}

//==============================================================================
void TrackListView::updateRecordingLane()
{
    // La vista previa se dibuja sobre la zona de clips de la pista en la que se graba.
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
    if (engine.isRecording() && seconds > totalLength - 2.0)
    {
        totalLength = seconds * 1.25;
        setVisibleRange (fitToWindow ? 0.0 : visibleStart, fitToWindow ? totalLength : visibleLength);
    }

    // Con zoom, la vista sigue al cabezal mientras suena.
    if (engine.getTransport().isPlaying() && ! fitToWindow
        && (seconds > visibleStart + visibleLength * 0.97 || seconds < visibleStart))
        setVisibleRange (seconds - visibleLength * 0.05, visibleLength);

    ruler.setPlayheadSeconds (seconds);

    const auto relative = (seconds - visibleStart) / visibleLength;
    const auto x = playhead.getWidth() > 0 && relative >= 0.0 && relative <= 1.0
                 ? juce::roundToInt (relative * playhead.getWidth())
                 : -1;

    if (x != playhead.x)
    {
        // Solo se repintan las dos franjas afectadas: las formas de onda están
        // cacheadas como imagen.
        if (playhead.x == -2)
            playhead.repaint();
        else
            playhead.repaint (playhead.x - 2, 0, 5, playhead.getHeight());

        playhead.x = x;
        playhead.repaint (x - 2, 0, 5, playhead.getHeight());
    }
}
}
