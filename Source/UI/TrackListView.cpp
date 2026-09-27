#include "TrackListView.h"

#include "Utils/Strings.h"

#include <algorithm>
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

    enum TrackMenuIds { renameId = 1, addBelowId, moveUpId, moveDownId, deleteId, copyId, cutId, pasteBelowId };
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
                          getLocalBounds().withTrimmedTop (AddTrackButton::height + 6).reduced (20),
                          juce::Justification::centred, 3);
    }
}

//==============================================================================
juce::Point<float> TrackListView::AddTrackButton::getCircleCentre() const
{
    // En la esquina derecha de la zona de cabeceras, centrado sobre la línea.
    return { static_cast<float> (getWidth()) - radius - 6.0f, static_cast<float> (getHeight()) * 0.5f };
}

bool TrackListView::AddTrackButton::hitTest (int x, int y)
{
    // Solo el círculo responde al ratón; lo demás sigue siendo la pista de debajo.
    return getCircleCentre().getDistanceFrom ({ static_cast<float> (x), static_cast<float> (y) }) <= radius + 3.0f;
}

void TrackListView::AddTrackButton::paint (juce::Graphics& g)
{
    const auto centre = getCircleCentre();
    const auto lineColour = colour.withAlpha (hovered ? 1.0f : 0.7f);

    // Línea del color de la pista, sin brillo, hasta el círculo.
    g.setColour (lineColour);
    g.fillRect (4.0f, centre.y - 1.0f, centre.x - 4.0f, 2.0f);

    g.setColour (Palette::panel.interpolatedWith (colour, hovered ? 0.4f : 0.2f));
    g.fillEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
    g.setColour (lineColour);
    g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.5f);

    g.setColour (hovered ? Palette::text : Palette::text.withAlpha (0.85f));
    g.fillRoundedRectangle (centre.x - 4.5f, centre.y - 0.75f, 9.0f, 1.5f, 0.75f);
    g.fillRoundedRectangle (centre.x - 0.75f, centre.y - 4.5f, 1.5f, 9.0f, 0.75f);
}

void TrackListView::AddTrackButton::mouseUp (const juce::MouseEvent& event)
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

    // Solo se dibuja en el hueco libre de la pista: lo que quede fuera (unos
    // milisegundos de latencia, o lo que pase del fragmento siguiente) se
    // recorta al terminar, y el audio que ya había no se tapa.
    const auto xForSample = [&] (juce::int64 sample)
    {
        return (static_cast<double> (sample) - visibleStart * sampleRate) * pixelsPerSample;
    };

    const auto left = std::max ({ 0.0, startX, xForSample (freeStart) });
    const auto right = std::min ({ static_cast<double> (bounds.getRight()), endX, xForSample (freeEnd) });
    const auto area = bounds.reduced (0, 3).toFloat();
    const auto region = juce::Rectangle<float>::leftTopRightBottom ((float) left, area.getY(), (float) right, area.getBottom());

    if (region.getWidth() <= 0.0f)
        return;

    g.setColour (Palette::record.withAlpha (0.15f));
    g.fillRoundedRectangle (region, 4.0f);

    g.setColour (Palette::record);
    const auto centreY = area.getCentreY();
    const auto halfHeight = area.getHeight() * 0.5f - 2.0f;
    int column = -1;
    float columnPeak = 0.0f;

    auto drawColumn = [&]
    {
        if (column >= region.getX() && column < region.getRight() && columnPeak > 0.0f)
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
    horizontalScroll.setAutoHide (false);
    horizontalScroll.setColour (juce::ScrollBar::thumbColourId, Palette::outline.brighter (0.4f));
    horizontalScroll.addListener (this);
    addAndMakeVisible (horizontalScroll);

    emptyAddButton.colour = Palette::accent;
    emptyAddButton.setTooltip ("Añadir una pista (Ctrl+T)"_u8);
    emptyAddButton.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    emptyAddButton.onClick = [this] { if (onAddTrack != nullptr) onAddTrack (-1); };
    content.addAndMakeVisible (emptyAddButton);

    addBelowButton.setTooltip ("Añadir una pista debajo"_u8);
    addBelowButton.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    addBelowButton.onClick = [this]
    {
        if (onAddTrack != nullptr && addBelowRow >= 0)
            onAddTrack (addBelowRow + 1);
    };
    content.addChildComponent (addBelowButton);

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
        row->onHeaderClicked = [this] (TrackView& view) { selectClip (view.getTrackPointer(), 0); };
        row->onSeek = [this] (double seconds) { seekTo (seconds); };
        row->onDelete = [this] (TrackView& view) { if (onDeleteRequested != nullptr) onDeleteRequested (view.getTrack()); };
        row->onRenamed = [this] (TrackView& view, const juce::String& oldName)
        {
            if (onTrackRenamed != nullptr)
                onTrackRenamed (view.getTrackPointer(), oldName);
        };
        row->onHeaderMenu = [this] (TrackView& view) { showTrackMenu (view); };
        row->onReorderDrag = [this] (TrackView& view, int parentY, int grabY) { reorderDrag (view, parentY, grabY); };
        row->onReorderEnd = [this] (TrackView& view) { reorderEnd (view); };
        row->onClipClicked = [this] (TrackView& view, juce::uint32 clipId) { selectClip (view.getTrackPointer(), clipId); };
        row->onClipsEdited = [this] (TrackView& view, std::vector<AudioClip> clipsBefore, const juce::String& actionName)
        {
            if (onClipsEdited != nullptr)
                onClipsEdited (view.getTrackPointer(), std::move (clipsBefore), actionName);
        };
        row->onWheel = [this] (int x, const juce::MouseEvent& e, const juce::MouseWheelDetails& w) { return handleWheel (x, e, w); };
        row->onContextMenu = [this] (TrackView& view, juce::uint32 clipId, double seconds)
        {
            if (onContextMenu != nullptr)
                onContextMenu (view.getTrackPointer(), clipId, seconds);
        };

        row->setVisibleRange (visibleStart, visibleLength);
        content.addAndMakeVisible (*row);
        newRows.push_back (std::move (row));
    }

    // Las filas de pistas eliminadas se destruyen aquí (y liberan su pista).
    rows = std::move (newRows);
    content.isEmpty = rows.empty();
    emptyAddButton.setVisible (rows.empty());
    addBelowButton.setVisible (false);
    addBelowRow = -1;

    recordingLane.toFront (false);
    playhead.toFront (false);
    addBelowButton.toFront (false);

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
int TrackListView::indexOf (const TrackView& view) const
{
    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i].get() == &view)
            return static_cast<int> (i);

    return -1;
}

void TrackListView::renameSelectedTrack()
{
    const auto current = selected.lock();

    for (auto& row : rows)
        if (row->getTrackPointer() == current)
            row->startRename();
}

void TrackListView::moveSelectedTrack (int direction)
{
    const auto current = selected.lock();

    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i]->getTrackPointer() == current)
        {
            moveTrack (static_cast<int> (i), static_cast<int> (i) + direction);
            return;
        }
    }
}

void TrackListView::moveTrack (int fromIndex, int toIndex)
{
    const auto count = static_cast<int> (rows.size());

    if (! juce::isPositiveAndBelow (fromIndex, count) || ! juce::isPositiveAndBelow (toIndex, count) || fromIndex == toIndex)
    {
        layoutRows();
        return;
    }

    const auto track = rows[static_cast<size_t> (fromIndex)]->getTrackPointer();
    engine.getMixer().moveTrack (fromIndex, toIndex);
    refresh();      // las filas se reordenan según el mezclador

    if (onTracksReordered != nullptr)
        onTracksReordered (track, fromIndex, toIndex);
}

void TrackListView::showTrackMenu (TrackView& view)
{
    const auto index = indexOf (view);

    if (index < 0)
        return;

    juce::PopupMenu menu;
    menu.addItem (renameId, "Cambiar nombre (F2)");
    menu.addItem (addBelowId, "Añadir pista debajo"_u8);
    menu.addSeparator();
    menu.addItem (copyId, "Copiar pista (Ctrl+C)");
    menu.addItem (cutId, "Cortar pista (Ctrl+X)");
    menu.addItem (pasteBelowId, "Pegar pista debajo (Ctrl+V)", canPasteTrack != nullptr && canPasteTrack());
    menu.addSeparator();
    menu.addItem (moveUpId, "Subir pista", index > 0);
    menu.addItem (moveDownId, "Bajar pista", index + 1 < static_cast<int> (rows.size()));
    menu.addSeparator();
    menu.addItem (deleteId, "Eliminar pista...");

    menu.showMenuAsync (juce::PopupMenu::Options(),
                        [safe = juce::Component::SafePointer<TrackListView> (this), track = view.getTrackPointer()] (int result)
    {
        if (safe == nullptr || result == 0)
            return;

        int index = -1;

        for (size_t i = 0; i < safe->rows.size(); ++i)
            if (safe->rows[i]->getTrackPointer() == track)
                index = static_cast<int> (i);

        if (index < 0)
            return;

        switch (result)
        {
            case renameId:      safe->rows[static_cast<size_t> (index)]->startRename(); break;
            case addBelowId:    if (safe->onAddTrack != nullptr) safe->onAddTrack (index + 1); break;
            case moveUpId:      safe->moveTrack (index, index - 1); break;
            case moveDownId:    safe->moveTrack (index, index + 1); break;
            case deleteId:      if (safe->onDeleteRequested != nullptr) safe->onDeleteRequested (*track); break;
            case copyId:        if (safe->onCopyTrack != nullptr) safe->onCopyTrack (track); break;
            case cutId:         if (safe->onCutTrack != nullptr) safe->onCutTrack (track); break;
            case pasteBelowId:  if (safe->onPasteTrack != nullptr) safe->onPasteTrack (index + 1); break;
            default:            break;
        }
    });
}

void TrackListView::reorderDrag (TrackView& view, int parentY, int grabY)
{
    const auto from = indexOf (view);
    const auto count = static_cast<int> (rows.size());

    if (from < 0 || count < 2)
        return;

    draggingRow = from;
    addBelowButton.setVisible (false);

    // La fila sigue al ratón...
    const auto h = TrackView::preferredHeight;
    const auto top = juce::jlimit (0, (count - 1) * h, parentY - grabY);
    view.setTopLeftPosition (0, top);
    view.toFront (false);
    playhead.toFront (false);

    // ...y las demás se apartan dejando el hueco donde caería.
    dropRow = juce::jlimit (0, count - 1, juce::roundToInt (static_cast<double> (top) / h));
    int slot = 0;

    for (int i = 0; i < count; ++i)
    {
        if (i == from)
            continue;

        if (slot == dropRow)
            ++slot;

        rows[static_cast<size_t> (i)]->setTopLeftPosition (0, slot * h);
        ++slot;
    }

    // Desplazar la lista si se arrastra cerca del borde.
    const auto inViewport = viewport.getLocalPoint (&content, juce::Point<int> (0, parentY));
    viewport.autoScroll (inViewport.x, inViewport.y, 30, 12);
}

void TrackListView::reorderEnd (TrackView& view)
{
    const auto from = indexOf (view);
    const auto to = dropRow;
    draggingRow = -1;
    dropRow = -1;

    moveTrack (from, to);
}

void TrackListView::updateAddButton()
{
    // El "+" aparece al pasar el ratón por una pista, centrado sobre su borde
    // inferior; mientras el ratón está sobre el propio "+" se mantiene.
    auto row = -1;

    if (! rows.empty() && draggingRow < 0 && content.isMouseOver (true))
    {
        if (addBelowButton.isVisible() && addBelowButton.isMouseOver())
        {
            row = addBelowRow;
        }
        else
        {
            const auto mouse = content.getMouseXYRelative();

            if (mouse.y >= 0)
                row = mouse.y / TrackView::preferredHeight;

            if (row >= static_cast<int> (rows.size()))
                row = -1;
        }
    }

    if (row < 0)
    {
        if (addBelowButton.isVisible())
            addBelowButton.setVisible (false);

        addBelowRow = -1;
        return;
    }

    if (row != addBelowRow || ! addBelowButton.isVisible())
    {
        addBelowRow = row;
        const auto border = rows[static_cast<size_t> (row)]->getBottom() - 1;
        addBelowButton.colour = rows[static_cast<size_t> (row)]->getColour();
        addBelowButton.setBounds (0, border - AddTrackButton::height / 2, TrackView::headerWidth - 1, AddTrackButton::height);
        addBelowButton.setVisible (true);
        addBelowButton.toFront (false);
        addBelowButton.repaint();
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
    top.removeFromLeft (TrackView::headerWidth);

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
    emptyAddButton.setBounds (0, 6, TrackView::headerWidth - 1, AddTrackButton::height);

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

    if (recordingLane.startSample.has_value())
    {
        const auto [gapStart, gapEnd] = ClipEditing::freeGapAt (targetRow->getTrack().getClips(), *recordingLane.startSample);

        if (gapStart != recordingLane.freeStart || gapEnd != recordingLane.freeEnd)
        {
            recordingLane.freeStart = gapStart;
            recordingLane.freeEnd = gapEnd;
            added = true;
        }
    }

    if (added)
        recordingLane.repaint();
}

void TrackListView::seekTo (double seconds)
{
    // Mientras se graba, la toma ocupa un tramo continuo desde donde empezó:
    // saltar la desalinearía con lo que suena.
    if (engine.isRecording())
        return;

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
    updateAddButton();

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
