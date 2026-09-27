#include "WaveformView.h"

#include "StemLabLookAndFeel.h"

namespace stemlab
{
namespace
{
    constexpr int edgeHandleWidth = 6;      // zona de agarre para recortar
    constexpr int dragThreshold = 3;        // píxeles antes de empezar a mover

    // Animación: constantes de tiempo (s) del deslizamiento y del levantamiento.
    // El clip levantado se estrecha un poco en vertical y sube, para que su
    // sombra se vea debajo.
    constexpr double slideTime = 0.07;
    constexpr double liftTime = 0.05;
    constexpr float liftInset = 5.0f;
    constexpr float liftHeight = 3.0f;
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

    syncShownStarts();
    repaint();
}

//==============================================================================
double WaveformView::shownStartOf (const AudioClip& clip) const
{
    const auto found = shownStarts.find (clip.id);
    return found != shownStarts.end() ? found->second : static_cast<double> (clip.timelineStart);
}

double WaveformView::getDrawnStart (juce::uint32 clipId) const
{
    if (clipId == floatingClip)
        return floatingStart;

    for (const auto& clip : clips)
        if (clip.id == clipId)
            return shownStartOf (clip);

    return 0.0;
}

void WaveformView::syncShownStarts()
{
    // Un clip nuevo aparece directamente en su sitio; uno que ya se veía y ha
    // cambiado de posición (se apartó, se deshizo un movimiento...) se desliza.
    std::map<juce::uint32, double> updated;
    auto needsAnimation = false;

    for (const auto& clip : clips)
    {
        const auto shown = shownStartOf (clip);
        updated[clip.id] = shown;
        needsAnimation = needsAnimation || ! juce::exactlyEqual (shown, static_cast<double> (clip.timelineStart));
    }

    shownStarts = std::move (updated);

    if (needsAnimation)
        startAnimation();
}

void WaveformView::startAnimation()
{
    if (! isTimerRunning())
    {
        lastFrameMs = juce::Time::getMillisecondCounterHiRes();
        startTimerHz (60);
    }
}

void WaveformView::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto elapsed = juce::jlimit (0.0, 0.1, (now - lastFrameMs) / 1000.0);
    lastFrameMs = now;

    // Acercamiento exponencial: rápido al principio y suave al final.
    const auto slide = 1.0 - std::exp (-elapsed / slideTime);
    const auto liftStep = static_cast<float> (1.0 - std::exp (-elapsed / liftTime));
    auto stillMoving = false;

    for (const auto& clip : clips)
    {
        if (clip.id == floatingClip || clip.source == nullptr)
            continue;

        auto& shown = shownStarts[clip.id];
        const auto target = static_cast<double> (clip.timelineStart);
        const auto quarterPixel = 0.25 * visibleLength * clip.source->sampleRate / juce::jmax (1, getWidth());

        if (std::abs (target - shown) > quarterPixel)
        {
            shown += (target - shown) * slide;
            stillMoving = true;
        }
        else
        {
            shown = target;
        }
    }

    const auto liftTarget = floatingClip != 0 ? 1.0f : 0.0f;

    if (std::abs (liftTarget - lift) > 0.01f)
    {
        lift += (liftTarget - lift) * liftStep;
        stillMoving = true;
    }
    else
    {
        lift = liftTarget;
    }

    if (! stillMoving)
    {
        stopTimer();

        if (floatingClip == 0)
            topClip = 0;
    }

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
    return xForPosition (static_cast<double> (sample), sampleRate);
}

float WaveformView::xForPosition (double sample, double sampleRate) const
{
    return static_cast<float> ((sample / sampleRate - visibleStart) / visibleLength * getWidth());
}

//==============================================================================
void WaveformView::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background);

    const AudioClip* top = nullptr;

    // En orden: los clips posteriores quedan por encima. El levantado, al final.
    for (const auto& clip : clips)
    {
        if (clip.id == topClip)
            top = &clip;
        else
            drawClip (g, clip, shownStartOf (clip), 0.0f);
    }

    if (top == nullptr || top->source == nullptr)
        return;

    const auto start = top->id == floatingClip ? floatingStart : shownStartOf (*top);

    // Lo que queda debajo del clip levantado se oscurece: se ve que pasa por encima.
    if (lift > 0.0f)
    {
        const auto end = start + static_cast<double> (top->length);
        g.setColour (juce::Colours::black.withAlpha (0.35f * lift));

        for (const auto& clip : clips)
        {
            if (clip.id == top->id || clip.source == nullptr)
                continue;

            const auto otherStart = shownStartOf (clip);
            const auto from = juce::jmax (start, otherStart);
            const auto to = juce::jmin (end, otherStart + static_cast<double> (clip.length));

            if (to > from)
                g.fillRoundedRectangle (juce::Rectangle<float>::leftTopRightBottom (xForPosition (from, clip.source->sampleRate), 3.0f,
                                                                                   xForPosition (to, clip.source->sampleRate),
                                                                                   (float) getHeight() - 3.0f), 4.0f);
        }
    }

    drawClip (g, *top, start, lift);
}

void WaveformView::drawClip (juce::Graphics& g, const AudioClip& clip, double drawnStart, float liftAmount) const
{
    if (clip.source == nullptr)
        return;

    const auto rate = clip.source->sampleRate;
    const auto x0 = xForPosition (drawnStart, rate);
    const auto x1 = xForPosition (drawnStart + static_cast<double> (clip.length), rate);

    if (x1 - x0 < 1.0f || x1 < 0.0f || x0 > (float) getWidth())
        return;

    const auto area = juce::Rectangle<float>::leftTopRightBottom (x0, 3.0f, x1, (float) getHeight() - 3.0f)
                          .reduced (0.0f, liftInset * liftAmount)
                          .translated (0.0f, -liftHeight * liftAmount);
    const auto isSelected = clip.id == selectedClip;

    if (liftAmount > 0.0f)
    {
        // Sombra más grande y más abajo cuanto más levantado está.
        juce::Path shape;
        shape.addRoundedRectangle (area, 4.0f);
        juce::DropShadow (juce::Colours::black.withAlpha (0.8f * liftAmount),
                          juce::roundToInt (4.0f + 10.0f * liftAmount),
                          { 0, juce::roundToInt (2.0f + 5.0f * liftAmount) }).drawForPath (g, shape);

        // Halo del color de la pista alrededor.
        g.setColour (waveColour.withAlpha (0.35f * liftAmount));
        g.drawRoundedRectangle (area.expanded (1.5f), 5.5f, 2.0f);
    }

    // Tapar lo que haya debajo. Levantado deja entrever lo de abajo: se ve que
    // pasa por encima.
    g.setColour (Palette::background.withAlpha (1.0f - 0.35f * liftAmount));
    g.fillRoundedRectangle (area, 4.0f);

    g.setColour (waveColour.withAlpha (dimmed ? 0.05f : (isSelected ? 0.3f : 0.12f + 0.12f * liftAmount)));
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

    g.setColour (isSelected ? Palette::text : waveColour.withAlpha (0.5f + 0.5f * liftAmount));
    g.drawRoundedRectangle (area, 4.0f, isSelected || liftAmount > 0.0f ? 1.5f : 1.0f);

    // Asas de recorte del clip seleccionado (no mientras se arrastra).
    if (isSelected && liftAmount <= 0.0f && area.getWidth() > 3.0f * edgeHandleWidth)
    {
        g.setColour (Palette::text.withAlpha (0.8f));
        g.fillRoundedRectangle (area.withWidth (3.0f).withSizeKeepingCentre (3.0f, area.getHeight() * 0.4f), 1.5f);
        g.fillRoundedRectangle (area.withLeft (area.getRight() - 3.0f).withSizeKeepingCentre (3.0f, area.getHeight() * 0.4f), 1.5f);
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
        clipsBeforeDrag = track.getClips();
        dragStartX = event.x;
        clickSeconds = secondsForX (event.x);
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

    // Siempre se parte de la lista de antes de arrastrar: si se vuelve atrás,
    // los clips que se apartaron regresan a su sitio.
    std::vector<AudioClip> updated;

    if (dragMode == DragMode::move)
    {
        updated = ClipEditing::moveWithoutOverlap (clipsBeforeDrag, dragOriginal.id, dragOriginal.timelineStart + delta);

        // Se "levanta" y se dibuja justo bajo el ratón, aunque pase por encima
        // de otro clip; la pista ya tiene su posición real (la que sonará).
        floatingClip = topClip = dragOriginal.id;
        floatingStart = static_cast<double> (juce::jmax<juce::int64> (0, dragOriginal.timelineStart + delta));
        startAnimation();
    }
    else
    {
        // Recortar: el borde se detiene en el vecino.
        const auto [lowest, highest] = ClipEditing::freeRangeAround (clipsBeforeDrag, dragOriginal);
        auto edited = dragOriginal;

        if (dragMode == DragMode::trimStart)
            ClipEditing::trimStart (edited, juce::jmax (lowest, dragOriginal.timelineStart + delta));
        else
            ClipEditing::trimEnd (edited, juce::jmin (highest, dragOriginal.getEnd() + delta));

        updated = clipsBeforeDrag;

        if (auto* clip = ClipEditing::find (updated, edited.id))
            *clip = edited;
    }

    // Se aplica a la pista en cada movimiento para oír el resultado al momento.
    track.setClips (updated);
    clips = std::move (updated);
    syncShownStarts();
    dragChanged = true;
    repaint();
}

void WaveformView::mouseUp (const juce::MouseEvent&)
{
    const auto mode = dragMode;
    const auto changed = dragChanged;
    dragMode = DragMode::none;
    dragChanged = false;

    // Al soltar, el clip baja desde donde está el ratón hasta su sitio real.
    if (floatingClip != 0)
    {
        shownStarts[floatingClip] = floatingStart;
        floatingClip = 0;
        startAnimation();
    }

    if (mode == DragMode::none || mode == DragMode::seek)
        return;

    if (changed)
    {
        if (onClipsEdited != nullptr)
            onClipsEdited (std::move (clipsBeforeDrag), mode == DragMode::move ? "Mover fragmento" : "Recortar fragmento");
    }
    else if (onSeek != nullptr)
    {
        // Clic sin arrastrar sobre un clip: además de seleccionarlo, el cabezal
        // va ahí (para grabar o pegar a continuación, o escuchar desde ese punto).
        onSeek (clickSeconds);
    }

    clipsBeforeDrag.clear();
}
}
