#include "WaveformView.h"

#include "StemLabLookAndFeel.h"

namespace stemlab
{
namespace
{
    constexpr int edgeHandleWidth = 6;      // zona de agarre para recortar
    constexpr int dragThreshold = 3;        // píxeles antes de empezar a mover
}

WaveformView::WaveformView (AudioTrack& audioTrack, juce::AudioFormatManager& formats,
                            juce::AudioThumbnailCache& thumbnailCache)
    : track (audioTrack), formatManager (formats), cache (thumbnailCache)
{
    setOpaque (true);
    setBufferedToImage (true);
    clipsChanged();
}

void WaveformView::setWaveColour (juce::Colour newColour)
{
    waveColour = newColour;
    repaint();
}

void WaveformView::setVisibleRange (double startSeconds, double lengthSeconds)
{
    startSeconds = juce::jmax (0.0, startSeconds);
    lengthSeconds = juce::jmax (0.01, lengthSeconds);

    if (! juce::exactlyEqual (visibleStart, startSeconds) || ! juce::exactlyEqual (visibleLength, lengthSeconds))
    {
        visibleStart = startSeconds;
        visibleLength = lengthSeconds;
        repaint();
    }
}

void WaveformView::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (onWheel == nullptr || ! onWheel (event.x, event, wheel))
        Component::mouseWheelMove (event, wheel);
}

void WaveformView::setDimmed (bool shouldBeDimmed)
{
    if (dimmed != shouldBeDimmed)
    {
        dimmed = shouldBeDimmed;
        repaint();
    }
}

void WaveformView::setSelectedClip (juce::uint32 clipId)
{
    if (selectedClip != clipId)
    {
        selectedClip = clipId;
        repaint();
    }
}

void WaveformView::clipsChanged()
{
    clips = track.getClips();

    // Miniaturas nuevas para archivos nuevos; se descartan las que ya no se usan.
    std::map<const ClipSource*, std::unique_ptr<juce::AudioThumbnail>> updated;

    for (const auto& clip : clips)
    {
        const auto* source = clip.source.get();

        if (source == nullptr || updated.count (source) > 0)
            continue;

        if (auto existing = thumbnails.find (source); existing != thumbnails.end())
        {
            updated[source] = std::move (existing->second);
            continue;
        }

        auto thumbnail = std::make_unique<juce::AudioThumbnail> (512, formatManager, cache);
        thumbnail->reset (source->audio.getNumChannels(), source->sampleRate, source->getLength());
        thumbnail->addBlock (0, source->audio, 0, source->audio.getNumSamples());
        updated[source] = std::move (thumbnail);

        // Las grabaciones con micrófonos integrados suelen quedar muy bajas y se
        // verían planas: se amplía el dibujo (no el sonido) hasta x20.
        float peak = 0.0f;

        for (int ch = 0; ch < source->audio.getNumChannels(); ++ch)
            peak = juce::jmax (peak, source->audio.getMagnitude (ch, 0, source->audio.getNumSamples()));

        verticalZooms[source] = peak > 0.0f ? juce::jlimit (1.0f, 20.0f, 0.9f / peak) : 1.0f;
    }

    thumbnails = std::move (updated);

    for (auto it = verticalZooms.begin(); it != verticalZooms.end();)
        it = thumbnails.count (it->first) > 0 ? std::next (it) : verticalZooms.erase (it);
    repaint();
}

juce::AudioThumbnail* WaveformView::thumbnailFor (const ClipSource* source) const
{
    const auto found = thumbnails.find (source);
    return found != thumbnails.end() ? found->second.get() : nullptr;
}

double WaveformView::secondsForX (int x) const
{
    return getWidth() > 0 ? juce::jmax (0.0, visibleStart + static_cast<double> (x) / getWidth() * visibleLength) : 0.0;
}

float WaveformView::xForSample (juce::int64 sample, double sampleRate) const
{
    return static_cast<float> ((static_cast<double> (sample) / sampleRate - visibleStart) / visibleLength * getWidth());
}

//==============================================================================
void WaveformView::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background);

    // En orden: los clips posteriores quedan (y suenan) por encima.
    for (const auto& clip : clips)
    {
        if (clip.source == nullptr)
            continue;

        const auto rate = clip.source->sampleRate;
        const auto x0 = xForSample (clip.timelineStart, rate);
        const auto x1 = xForSample (clip.getEnd(), rate);

        if (x1 - x0 < 1.0f || x1 < 0.0f || x0 > (float) getWidth())
            continue;

        const auto area = juce::Rectangle<float>::leftTopRightBottom (x0, 3.0f, x1, (float) getHeight() - 3.0f);
        const auto isSelected = clip.id == selectedClip;

        // Tapar lo que haya debajo (clips solapados).
        g.setColour (Palette::background);
        g.fillRect (area);

        g.setColour (waveColour.withAlpha (dimmed ? 0.05f : (isSelected ? 0.3f : 0.12f)));
        g.fillRoundedRectangle (area, 4.0f);

        if (auto* thumbnail = thumbnailFor (clip.source.get()))
        {
            // Con mucho zoom un clip puede medir cientos de miles de píxeles:
            // solo se dibuja el trozo visible, con su tramo de tiempo.
            const auto visible = area.getIntersection (getLocalBounds().toFloat());
            const auto clipStartTime = static_cast<double> (clip.sourceOffset) / rate;
            const auto clipDuration = static_cast<double> (clip.length) / rate;
            const auto t0 = clipStartTime + (visible.getX() - x0) / (x1 - x0) * clipDuration;
            const auto t1 = clipStartTime + (visible.getRight() - x0) / (x1 - x0) * clipDuration;

            const auto zoom = verticalZooms.count (clip.source.get()) > 0 ? verticalZooms.at (clip.source.get()) : 1.0f;
            g.setColour (dimmed ? waveColour.withAlpha (0.3f) : waveColour);
            thumbnail->drawChannels (g, visible.reduced (1.0f, 2.0f).toNearestInt(), t0, t1, zoom);
        }

        g.setColour (isSelected ? Palette::text : waveColour.withAlpha (0.5f));
        g.drawRoundedRectangle (area, 4.0f, isSelected ? 1.5f : 1.0f);

        // Asas de recorte del clip seleccionado.
        if (isSelected && area.getWidth() > 3.0f * edgeHandleWidth)
        {
            g.setColour (Palette::text.withAlpha (0.8f));
            g.fillRoundedRectangle (area.withWidth (3.0f).withSizeKeepingCentre (3.0f, area.getHeight() * 0.4f), 1.5f);
            g.fillRoundedRectangle (area.withLeft (area.getRight() - 3.0f).withSizeKeepingCentre (3.0f, area.getHeight() * 0.4f), 1.5f);
        }
    }
}

//==============================================================================
WaveformView::ClipHit WaveformView::findClipAt (int x) const
{
    // Del último al primero: el clip que se ve encima es el que se agarra.
    for (auto it = clips.rbegin(); it != clips.rend(); ++it)
    {
        if (it->source == nullptr)
            continue;

        const auto x0 = xForSample (it->timelineStart, it->source->sampleRate);
        const auto x1 = xForSample (it->getEnd(), it->source->sampleRate);
        const auto fx = static_cast<float> (x);

        if (fx < x0 || fx > x1)
            continue;

        const auto wide = x1 - x0 > 3.0f * edgeHandleWidth;

        if (wide && fx - x0 <= edgeHandleWidth)   return { it->id, DragMode::trimStart };
        if (wide && x1 - fx <= edgeHandleWidth)   return { it->id, DragMode::trimEnd };

        return { it->id, DragMode::move };
    }

    return {};
}

void WaveformView::mouseMove (const juce::MouseEvent& event)
{
    const auto hit = findClipAt (event.x);

    switch (hit.mode)
    {
        case DragMode::trimStart:
        case DragMode::trimEnd:     setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); break;
        case DragMode::move:        setMouseCursor (juce::MouseCursor::DraggingHandCursor); break;
        case DragMode::none:
        case DragMode::seek:        setMouseCursor (juce::MouseCursor::NormalCursor); break;
    }
}

void WaveformView::mouseDown (const juce::MouseEvent& event)
{
    const auto hit = findClipAt (event.x);
    dragMode = DragMode::none;
    dragChanged = false;

    if (onClipClicked != nullptr)
        onClipClicked (hit.clipId);

    if (event.mods.isPopupMenu())
    {
        if (onContextMenu != nullptr)
            onContextMenu (hit.clipId, secondsForX (event.x));

        return;
    }

    if (hit.clipId == 0)
    {
        dragMode = DragMode::seek;

        if (onSeek != nullptr)
            onSeek (secondsForX (event.x));

        return;
    }

    if (auto* clip = ClipEditing::find (clips, hit.clipId))
    {
        dragMode = hit.mode;
        dragOriginal = *clip;
        dragStartX = event.x;
    }
}

void WaveformView::mouseDrag (const juce::MouseEvent& event)
{
    if (dragMode == DragMode::seek)
    {
        if (onSeek != nullptr)
            onSeek (secondsForX (event.x));

        return;
    }

    if (dragMode == DragMode::none || dragOriginal.source == nullptr || getWidth() <= 0)
        return;

    const auto dx = event.x - dragStartX;

    if (! dragChanged && std::abs (dx) < dragThreshold)
        return;

    const auto samplesPerPixel = visibleLength * dragOriginal.source->sampleRate / getWidth();
    const auto delta = static_cast<juce::int64> (std::llround (dx * samplesPerPixel));

    auto edited = dragOriginal;

    switch (dragMode)
    {
        case DragMode::move:        ClipEditing::move (edited, dragOriginal.timelineStart + delta); break;
        case DragMode::trimStart:   ClipEditing::trimStart (edited, dragOriginal.timelineStart + delta); break;
        case DragMode::trimEnd:     ClipEditing::trimEnd (edited, dragOriginal.getEnd() + delta); break;
        case DragMode::none:
        case DragMode::seek:        return;
    }

    // Se aplica a la pista en cada movimiento para oír el resultado al momento.
    auto updated = track.getClips();

    if (auto* clip = ClipEditing::find (updated, edited.id))
    {
        *clip = edited;
        track.setClips (updated);
        clips = std::move (updated);
        dragChanged = true;
        repaint();
    }
}

void WaveformView::mouseUp (const juce::MouseEvent&)
{
    if (dragChanged && onClipsEdited != nullptr)
        onClipsEdited();

    dragMode = DragMode::none;
    dragChanged = false;
}
}
