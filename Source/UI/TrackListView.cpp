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

    enum TrackMenuIds { renameId = 1, addBelowId, moveUpId, moveDownId, deleteId, copyId, cutId, pasteBelowId,
                        leaveFolderId, intoFolderBaseId = 100 };

    enum FolderMenuIds { toggleFolderId = 1, toggleWavesId, deleteFolderId };
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

    // Con el aspecto de un fragmento de la pista (su color), no en rojo: al
    // terminar, la toma queda igual que se veía mientras se grababa.
    g.setColour (colour.withAlpha (0.12f));
    g.fillRoundedRectangle (region, 4.0f);
    g.setColour (colour.withAlpha (0.5f));
    g.drawRoundedRectangle (region, 4.0f, 1.0f);

    g.setColour (colour);
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

//==============================================================================
TrackListView::FolderHeader::FolderHeader()
{
    wavesButton.setButtonText ("Ondas");
    wavesButton.setTooltip ("Abrir o cerrar la ventana de ondas de la separación"_u8);
    wavesButton.onClick = [this] { if (onToggleWaves != nullptr) onToggleWaves(); };
    addAndMakeVisible (wavesButton);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void TrackListView::FolderHeader::setInfo (const FolderInfo& newInfo, int memberCount)
{
    info = newInfo;
    members = memberCount;
    wavesButton.setEnabled (info.canShowWaves);
    wavesButton.setToggleState (info.wavesOpen, juce::dontSendNotification);
    wavesButton.setColour (juce::TextButton::buttonOnColourId, info.colour.withAlpha (0.8f));
    repaint();
}

void TrackListView::FolderHeader::paint (juce::Graphics& g)
{
    auto header = getLocalBounds().removeFromLeft (TrackView::headerWidth).toFloat();

    // Cabecera teñida del color de la carpeta y, a la derecha, una banda suave
    // a lo largo de la línea de tiempo.
    g.setColour (Palette::panel.interpolatedWith (info.colour, 0.14f));
    g.fillRect (header);
    g.setColour (info.colour.withAlpha (0.06f));
    g.fillRect (getLocalBounds().withTrimmedLeft (TrackView::headerWidth));

    g.setColour (info.colour);
    g.fillRect (header.removeFromLeft (6.0f));

    // Flecha: hacia abajo desplegada, hacia la derecha plegada.
    const auto centreY = (float) getHeight() * 0.5f;
    juce::Path arrow;

    if (info.expanded)
        arrow.addTriangle (14.0f, centreY - 3.0f, 24.0f, centreY - 3.0f, 19.0f, centreY + 3.5f);
    else
        arrow.addTriangle (16.0f, centreY - 5.0f, 16.0f, centreY + 5.0f, 22.5f, centreY);

    g.setColour (Palette::text);
    g.fillPath (arrow);

    // Icono de carpeta (pestaña + cuerpo).
    const juce::Rectangle<float> body (32.0f, centreY - 6.0f, 20.0f, 13.0f);
    g.setColour (info.colour);
    g.fillRoundedRectangle (body.withTrimmedRight (11.0f).translated (0.0f, -3.0f).withHeight (5.0f), 1.5f);
    g.fillRoundedRectangle (body, 2.0f);

    // Nombre y número de pistas.
    const auto textArea = juce::Rectangle<float> (60.0f, 0.0f, (float) wavesButton.getX() - 66.0f, (float) getHeight());
    g.setColour (Palette::text);
    g.setFont (juce::FontOptions (13.5f, juce::Font::bold));
    g.drawText (info.name, textArea.withTrimmedBottom ((float) getHeight() * 0.42f).withTrimmedTop (2.0f),
                juce::Justification::bottomLeft, true);
    g.setColour (Palette::textDim);
    g.setFont (juce::FontOptions (11.5f));
    g.drawText (juce::String (members) + (members == 1 ? " pista" : " pistas"),
                textArea.withTrimmedTop ((float) getHeight() * 0.55f), juce::Justification::topLeft, true);

    g.setColour (Palette::outline);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
    g.drawVerticalLine (TrackView::headerWidth - 1, 0.0f, (float) getHeight());
}

void TrackListView::FolderHeader::resized()
{
    wavesButton.setBounds (TrackView::headerWidth - 8 - 64, (getHeight() - 22) / 2, 64, 22);
}

void TrackListView::FolderHeader::mouseUp (const juce::MouseEvent& event)
{
    // Clic en la cabecera: desplegar o plegar la carpeta. Clic derecho: su menú.
    if (! event.mouseWasClicked())
        return;

    if (event.mods.isPopupMenu())
    {
        if (onMenu != nullptr)
            onMenu();
    }
    else if (onToggle != nullptr)
    {
        onToggle();
    }
}

//==============================================================================
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
    emptyAddButton.onClick = [this] { if (onAddTrack != nullptr) onAddTrack (-1, {}); };
    content.addAndMakeVisible (emptyAddButton);

    addBelowButton.setTooltip ("Añadir una pista debajo"_u8);
    addBelowButton.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    addBelowButton.onClick = [this]
    {
        // El "+" de una pista de carpeta añade la nueva dentro de la carpeta.
        if (onAddTrack != nullptr && juce::isPositiveAndBelow (addBelowRow, (int) rows.size()))
        {
            const auto& folderId = rows[(size_t) addBelowRow]->getTrack().getFolderId();
            onAddTrack (addBelowRow + 1, findFolder (folderId) != nullptr ? folderId : juce::String());
        }
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
    rebuildEntries();
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

//==============================================================================
void TrackListView::setFolders (std::vector<FolderInfo> newFolders)
{
    folders = std::move (newFolders);

    // Una cabecera por carpeta; las de carpetas que ya no existen se quitan.
    for (auto it = folderHeaders.begin(); it != folderHeaders.end();)
        it = findFolder (it->first) == nullptr ? folderHeaders.erase (it) : std::next (it);

    for (const auto& folder : folders)
    {
        auto& header = folderHeaders[folder.id];

        if (header == nullptr)
        {
            header = std::make_unique<FolderHeader>();
            const auto id = folder.id;
            header->onToggle = [this, id] { if (onToggleFolder != nullptr) onToggleFolder (id); };
            header->onToggleWaves = [this, id] { if (onToggleFolderWindow != nullptr) onToggleFolderWindow (id); };
            header->onMenu = [this, id] { showFolderMenu (id); };
            content.addChildComponent (*header);
        }
    }

    rebuildEntries();
    layoutRows();
}

const TrackListView::FolderInfo* TrackListView::findFolder (const juce::String& folderId) const
{
    if (folderId.isEmpty())
        return nullptr;

    for (const auto& folder : folders)
        if (folder.id == folderId)
            return &folder;

    return nullptr;
}

juce::Rectangle<int> TrackListView::getFolderHeaderBounds (const juce::String& folderId) const
{
    const auto found = folderHeaders.find (folderId);
    return found != folderHeaders.end() && found->second->isVisible() ? found->second->getBounds() : juce::Rectangle<int>();
}

void TrackListView::rebuildEntries()
{
    // Las pistas se ven en el orden del mezclador; las de una carpeta, juntas
    // bajo su cabecera (donde está la primera), y ocultas si está plegada.
    entries.clear();
    std::vector<juce::String> shown;

    for (auto& [id, header] : folderHeaders)
        header->setVisible (false);

    for (auto& row : rows)
    {
        const auto& folderId = row->getTrack().getFolderId();
        const auto* folder = findFolder (folderId);

        if (folder == nullptr)
        {
            row->setFolderColour (juce::Colours::transparentBlack);
            row->setVisible (true);
            entries.push_back ({ nullptr, row.get(), {} });
            continue;
        }

        if (std::find (shown.begin(), shown.end(), folderId) != shown.end())
            continue;

        shown.push_back (folderId);
        auto* header = folderHeaders[folderId].get();
        const auto members = std::count_if (rows.begin(), rows.end(),
                                            [&] (const auto& r) { return r->getTrack().getFolderId() == folderId; });
        header->setInfo (*folder, static_cast<int> (members));
        header->setVisible (true);
        entries.push_back ({ header, nullptr, folderId });

        for (auto& member : rows)
        {
            if (member->getTrack().getFolderId() != folderId)
                continue;

            member->setFolderColour (folder->colour);
            member->setVisible (folder->expanded);

            if (folder->expanded)
                entries.push_back ({ nullptr, member.get(), folderId });
        }
    }
}

int TrackListView::heightOf (const Entry& entry)
{
    return entry.header != nullptr ? FolderHeader::height : TrackView::preferredHeight;
}

int TrackListView::mixerIndexAtEndOf (const juce::String& folderId, const std::shared_ptr<AudioTrack>& excluding) const
{
    // Posición en el mezclador (sin contar "excluding") justo después de la
    // última pista de la carpeta; al final si no tiene ninguna.
    int index = 0, afterLast = -1;

    for (const auto& track : engine.getMixer().getTracks())
    {
        if (track == excluding)
            continue;

        ++index;

        if (track->getFolderId() == folderId)
            afterLast = index;
    }

    return afterLast >= 0 ? afterLast : index;
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

void TrackListView::showFolderMenu (const juce::String& folderId)
{
    const auto* folder = findFolder (folderId);

    if (folder == nullptr)
        return;

    juce::PopupMenu menu;
    menu.addItem (toggleFolderId, folder->expanded ? "Plegar carpeta" : "Desplegar carpeta");
    menu.addItem (toggleWavesId, folder->wavesOpen ? "Cerrar ondas" : "Abrir ondas", folder->canShowWaves);
    menu.addSeparator();
    menu.addItem (deleteFolderId, "Eliminar carpeta...");

    menu.showMenuAsync (juce::PopupMenu::Options(),
                        [safe = juce::Component::SafePointer<TrackListView> (this), folderId] (int result)
    {
        if (safe == nullptr || safe->findFolder (folderId) == nullptr)
            return;

        switch (result)
        {
            case toggleFolderId:    if (safe->onToggleFolder != nullptr) safe->onToggleFolder (folderId); break;
            case toggleWavesId:     if (safe->onToggleFolderWindow != nullptr) safe->onToggleFolderWindow (folderId); break;
            case deleteFolderId:    if (safe->onDeleteFolderRequested != nullptr) safe->onDeleteFolderRequested (folderId); break;
            default:                break;
        }
    });
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

    // Carpetas: sacar de la suya o meter en otra (de las que se ven).
    const auto* current = findFolder (view.getTrack().getFolderId());
    juce::PopupMenu into;

    for (size_t i = 0; i < folders.size(); ++i)
        if (&folders[i] != current && getFolderHeaderBounds (folders[i].id) != juce::Rectangle<int>())
            into.addItem (intoFolderBaseId + (int) i, folders[i].name);

    if (current != nullptr || into.getNumItems() > 0)
    {
        menu.addSeparator();

        if (current != nullptr)
            menu.addItem (leaveFolderId, "Sacar de la carpeta \"" + current->name + "\"");

        menu.addSubMenu ("Meter en la carpeta", into, into.getNumItems() > 0);
    }

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
            case addBelowId:
                if (safe->onAddTrack != nullptr)
                    safe->onAddTrack (index + 1, safe->findFolder (track->getFolderId()) != nullptr ? track->getFolderId() : juce::String());
                break;
            case moveUpId:      safe->moveTrack (index, index - 1); break;
            case moveDownId:    safe->moveTrack (index, index + 1); break;
            case deleteId:      if (safe->onDeleteRequested != nullptr) safe->onDeleteRequested (*track); break;
            case copyId:        if (safe->onCopyTrack != nullptr) safe->onCopyTrack (track); break;
            case cutId:         if (safe->onCutTrack != nullptr) safe->onCutTrack (track); break;
            case pasteBelowId:  if (safe->onPasteTrack != nullptr) safe->onPasteTrack (index + 1); break;

            case leaveFolderId:
                // Fuera, justo debajo de la carpeta.
                if (safe->onTrackDropped != nullptr)
                    safe->onTrackDropped (track, {}, safe->mixerIndexAtEndOf (track->getFolderId(), track));
                break;

            default:
                if (result >= intoFolderBaseId && safe->onTrackDropped != nullptr
                    && juce::isPositiveAndBelow (result - intoFolderBaseId, (int) safe->folders.size()))
                {
                    const auto folderId = safe->folders[(size_t) (result - intoFolderBaseId)].id;
                    safe->onTrackDropped (track, folderId, safe->mixerIndexAtEndOf (folderId, track));
                }
                break;
        }
    });
}

void TrackListView::reorderDrag (TrackView& view, int parentY, int grabY)
{
    const auto from = indexOf (view);

    if (from < 0 || rows.size() < 2)
        return;

    draggingRow = from;
    addBelowButton.setVisible (false);

    // Lo que se ve sin la pista arrastrada, colocado sin huecos.
    std::vector<Entry> others;

    for (const auto& entry : entries)
        if (entry.row != &view)
            others.push_back (entry);

    std::vector<int> tops;
    int total = 0;

    for (const auto& entry : others)
    {
        tops.push_back (total);
        total += heightOf (entry);
    }

    // La fila sigue al ratón.
    const auto h = view.getHeight();
    const auto top = juce::jlimit (0, total, parentY - grabY);
    view.setTopLeftPosition (0, top);
    view.toFront (false);
    playhead.toFront (false);

    // Sitios donde puede caer: antes de cada pista o carpeta (fuera), justo
    // bajo una cabecera, entre sus pistas y al final de su bloque (dentro),
    // sobre una cabecera plegada (dentro) y al final de la lista.
    const auto firstMember = [this, &view] (const juce::String& folderId) -> std::shared_ptr<AudioTrack>
    {
        for (const auto& row : rows)
            if (row.get() != &view && row->getTrack().getFolderId() == folderId)
                return row->getTrackPointer();

        return nullptr;
    };

    std::vector<DropSlot> slots;
    const auto count = others.size();

    for (size_t i = 0; i < count; ++i)
    {
        const auto& entry = others[i];
        const auto entryHeight = heightOf (entry);

        if (entry.header != nullptr)
        {
            slots.push_back ({ tops[i], i, {}, firstMember (entry.folderId) });

            const auto* folder = findFolder (entry.folderId);
            const auto hasMembersBelow = i + 1 < count && others[i + 1].row != nullptr && others[i + 1].folderId == entry.folderId;

            if (folder != nullptr && folder->expanded)
                slots.push_back ({ tops[i] + entryHeight, i + 1, entry.folderId,
                                   hasMembersBelow ? others[i + 1].row->getTrackPointer() : nullptr });
            else
                slots.push_back ({ tops[i] + entryHeight / 2, i + 1, entry.folderId, nullptr });
        }
        else if (entry.folderId.isEmpty())
        {
            slots.push_back ({ tops[i], i, {}, entry.row->getTrackPointer() });
        }
        else
        {
            if (i > 0 && others[i - 1].header == nullptr)
                slots.push_back ({ tops[i], i, entry.folderId, entry.row->getTrackPointer() });

            const auto lastOfFolder = i + 1 == count || others[i + 1].header != nullptr || others[i + 1].folderId != entry.folderId;

            if (lastOfFolder)
                slots.push_back ({ tops[i] + entryHeight - 20, i + 1, entry.folderId, nullptr });
        }
    }

    slots.push_back ({ total, count, {}, nullptr });

    // El más cercano al centro de la fila arrastrada.
    const auto centre = top + h / 2;
    const auto best = std::min_element (slots.begin(), slots.end(), [centre] (const DropSlot& a, const DropSlot& b)
    {
        return std::abs (a.y - centre) < std::abs (b.y - centre);
    });

    drop = *best;

    // Las demás se apartan dejando el hueco; la arrastrada muestra la franja
    // de la carpeta en la que caería.
    int y = 0;

    for (size_t i = 0; i < count; ++i)
    {
        if (i == drop->insertAt)
            y += h;

        auto* component = others[i].header != nullptr ? static_cast<juce::Component*> (others[i].header)
                                                      : static_cast<juce::Component*> (others[i].row);
        component->setTopLeftPosition (0, y);
        y += heightOf (others[i]);
    }

    const auto* target = findFolder (drop->folderId);
    view.setFolderColour (target != nullptr ? target->colour : juce::Colours::transparentBlack);

    // Desplazar la lista si se arrastra cerca del borde.
    const auto inViewport = viewport.getLocalPoint (&content, juce::Point<int> (0, parentY));
    viewport.autoScroll (inViewport.x, inViewport.y, 30, 12);
}

void TrackListView::reorderEnd (TrackView& view)
{
    const auto track = view.getTrackPointer();
    const auto slot = drop;
    draggingRow = -1;
    drop.reset();

    if (! slot.has_value() || onTrackDropped == nullptr)
    {
        refresh();
        return;
    }

    // Posición en el mezclador, contando sin la pista arrastrada.
    int index = 0;

    if (slot->before != nullptr)
    {
        for (const auto& other : engine.getMixer().getTracks())
        {
            if (other == slot->before)
                break;

            if (other != track)
                ++index;
        }
    }
    else if (slot->folderId.isNotEmpty())
    {
        index = mixerIndexAtEndOf (slot->folderId, track);
    }
    else
    {
        index = static_cast<int> (engine.getMixer().getTracks().size()) - 1;
    }

    onTrackDropped (track, slot->folderId, index);
    refresh();
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

            for (const auto& entry : entries)
                if (entry.row != nullptr && entry.row->getBounds().contains (0, mouse.y))
                    row = indexOf (*entry.row);
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
    int listHeight = 0;

    for (const auto& entry : entries)
        listHeight += heightOf (entry);

    const auto height = juce::jmax (viewport.getMaximumVisibleHeight(), listHeight);
    content.setSize (width, height);

    int y = 0;

    for (const auto& entry : entries)
    {
        auto* component = entry.header != nullptr ? static_cast<juce::Component*> (entry.header)
                                                  : static_cast<juce::Component*> (entry.row);
        component->setBounds (0, y, width, heightOf (entry));
        y += heightOf (entry);
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
            if (row->getTrack().isArmed() && row->isVisible())
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

    if (recordingLane.colour != targetRow->getColour())
    {
        recordingLane.colour = targetRow->getColour();
        added = true;
    }

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
