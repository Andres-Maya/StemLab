#include "SeparationWindow.h"

#include "StemLabLookAndFeel.h"
#include "Utils/Strings.h"

#include <algorithm>
#include <cmath>

namespace stemlab
{
namespace
{
    constexpr float twoPi = juce::MathConstants<float>::twoPi;
    constexpr double appearSeconds = 0.8;       // lo que tarda una pista nueva en salir y colocarse
    constexpr int headerHeight = 64;
    constexpr int footerHeight = 58;

    float easeOutCubic (float x)                { x = 1.0f - juce::jlimit (0.0f, 1.0f, x); return 1.0f - x * x * x; }

    float easeOutBack (float x)
    {
        // Se pasa un poco y vuelve: la pista "rebota" al llegar a su sitio.
        x = juce::jlimit (0.0f, 1.0f, x);
        constexpr float c1 = 1.70158f, c3 = c1 + 1.0f;
        return 1.0f + c3 * std::pow (x - 1.0f, 3.0f) + c1 * std::pow (x - 1.0f, 2.0f);
    }

    /** Esfera con luz arriba a la izquierda. */
    void fillSphere (juce::Graphics& g, juce::Point<float> centre, float radius, juce::Colour colour, float alpha)
    {
        juce::ColourGradient shade (colour.brighter (0.9f).withMultipliedAlpha (alpha),
                                    centre.translated (-radius * 0.35f, -radius * 0.4f),
                                    colour.darker (1.1f).withMultipliedAlpha (alpha),
                                    centre.translated (radius * 0.9f, radius * 0.9f), true);
        shade.addColour (0.45, colour.withMultipliedAlpha (alpha));
        g.setGradientFill (shade);
        g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));
    }

    void fillGlow (juce::Graphics& g, juce::Point<float> centre, float radius, juce::Colour colour, float alpha)
    {
        g.setGradientFill (juce::ColourGradient (colour.withAlpha (alpha), centre,
                                                 colour.withAlpha (0.0f), centre.translated (radius, 0.0f), true));
        g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));
    }
}

//==============================================================================
SeparationView::SeparationView (juce::String source, juce::Colour colour, std::vector<Stem> stemList)
    : sourceName (std::move (source)),
      sourceColour (colour),
      stems (std::move (stemList)),
      appearedAt (stems.size(), -1.0),
      present (stems.size(), true),
      presence (stems.size(), 1.0f)
{
    setOpaque (true);

    // Esta ventana es siempre oscura (DarkPalette), también con el tema claro.
    cancelButton.setButtonText (tr ("Cancelar separación"));
    cancelButton.setColour (juce::TextButton::buttonColourId, DarkPalette::panelLight);
    cancelButton.setColour (juce::TextButton::textColourOffId, DarkPalette::text);
    cancelButton.setColour (juce::ComboBox::outlineColourId, DarkPalette::outline);
    cancelButton.onClick = [this] { if (onCancel != nullptr) onCancel(); };
    addAndMakeVisible (cancelButton);

    setSize (620, 580);

    lastFrameMs = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (60);
}

void SeparationView::setSourceAudio (std::shared_ptr<const ClipSource> source, juce::int64 start, juce::int64 length)
{
    audio = std::move (source);
    audioStart = std::max<juce::int64> (0, start);
    audioLength = audio != nullptr ? juce::jmin (length, audio->getLength() - audioStart) : 0;
}

double SeparationView::appearanceThreshold (int index, int numStems)
{
    return static_cast<double> (index + 1) / static_cast<double> (numStems + 1);
}

int SeparationView::getNumVisibleStems() const noexcept
{
    int visible = 0;

    for (size_t i = 0; i < stems.size(); ++i)
        if (appearedAt[i] >= 0.0 && present[i])
            ++visible;

    return visible;
}

void SeparationView::setFinished (bool didSucceed)
{
    finished = true;
    succeeded = didSucceed;
    cancelButton.setVisible (false);
    advance (0.0);
}

void SeparationView::timerCallback()
{
    // El temporizador funciona desde que se crea la vista; solo se anima
    // mientras se ve (mostrar u ocultar la ventana no avisa a su contenido).
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto elapsed = juce::jlimit (0.0, 0.1, (now - lastFrameMs) / 1000.0);
    lastFrameMs = now;

    if (isShowing())
        advance (elapsed);
}

void SeparationView::advance (double seconds)
{
    time += seconds;

    if (getProgress != nullptr && ! finished)
        progress = getProgress();

    if (getStatus != nullptr && ! finished)
        status = getStatus();

    if (finished && succeeded)
    {
        progress = 1.0;
        status = tr ("¡Separación completada!");
    }

    // El número se acerca al progreso real sin saltos.
    if (progress >= 0.0)
        shownProgress += (progress - shownProgress) * (1.0 - std::exp (-seconds / 0.25));

    updateRing (seconds);

    // Cada pista aparece al pasar su porcentaje (todas al terminar bien).
    const auto numStems = static_cast<int> (stems.size());

    for (int i = 0; i < numStems; ++i)
        if (appearedAt[static_cast<size_t> (i)] < 0.0 && progress >= appearanceThreshold (i, numStems))
            appearedAt[static_cast<size_t> (i)] = time;

    // Cada onda sigue a su pista: se desvanece si se elimina y vuelve si se deshace.
    const auto fade = static_cast<float> (1.0 - std::exp (-seconds / 0.15));

    for (size_t i = 0; i < stems.size(); ++i)
    {
        present[i] = isStemPresent == nullptr || isStemPresent (static_cast<int> (i));
        const auto target = present[i] ? 1.0f : 0.0f;
        presence[i] = std::abs (target - presence[i]) < 0.01f ? target : presence[i] + (target - presence[i]) * fade;
    }

    repaint();
}

void SeparationView::updateRing (double seconds)
{
    // Cada punto del anillo resume unas pocas muestras: 360 puntos recorren
    // unos 45 ms de la canción, como un osciloscopio puesto en círculo.
    constexpr int samplesPerPoint = 6;
    constexpr int window = ringPoints * samplesPerPoint;
    std::vector<float> raw (ringPoints, 0.0f);

    if (audio != nullptr && audioLength > window)
    {
        // La posición avanza en tiempo real y vuelve a empezar al final.
        const auto elapsed = static_cast<juce::int64> (time * audio->sampleRate);
        const auto position = audioStart + 1 + elapsed % (audioLength - window - 1);
        const auto channels = audio->audio.getNumChannels();

        // Nivel de cada punto: un poco de la amplitud (la forma general) y
        // sobre todo el detalle agudo (diferencia con la muestra anterior):
        // batería, voces y platillos hacen los picos irregulares; el bajo, no.
        for (int k = 0; k < ringPoints; ++k)
        {
            float amplitude = 0.0f, detail = 0.0f;

            for (int ch = 0; ch < channels; ++ch)
            {
                const auto* data = audio->audio.getReadPointer (ch, static_cast<int> (position + k * samplesPerPoint));

                for (int i = 0; i < samplesPerPoint; ++i)
                {
                    amplitude = juce::jmax (amplitude, std::abs (data[i]));
                    detail = juce::jmax (detail, std::abs (data[i] - 0.95f * data[i - 1]));
                }
            }

            raw[(size_t) k] = 0.15f * amplitude + 1.3f * detail;
        }
    }
    else
    {
        // Sin audio: una señal que se parece a la música (picos irregulares
        // que se desplazan por el círculo).
        const auto t = time;

        for (int k = 0; k < ringPoints; ++k)
        {
            const auto x = static_cast<double> (k);
            const auto noise = std::fmod (std::abs (std::sin (x * 12.9898 + std::floor (t * 14.0) * 78.233) * 43758.5453), 1.0);
            const auto swell = 0.5 + 0.5 * std::sin (x * 0.035 - t * 1.4);
            raw[(size_t) k] = static_cast<float> (swell * swell * (0.35 * noise + 0.65 * std::abs (std::sin (x * 0.41 + t * 9.0))));
        }
    }

    // Como en los visualizadores: un círculo limpio con ráfagas de picos. Las
    // zonas del anillo con más energía que la típica del momento (golpes,
    // consonantes, platillos) se vuelven irregulares; el resto queda liso.
    constexpr int neighbourhood = 15;
    std::vector<float> energy ((size_t) ringPoints, 0.0f);

    for (int k = 0; k < ringPoints; ++k)
    {
        float sum = 0.0f;
        int count = 0;

        for (int j = juce::jmax (0, k - neighbourhood); j <= juce::jmin (ringPoints - 1, k + neighbourhood); ++j, ++count)
            sum += raw[(size_t) j];

        energy[(size_t) k] = sum / (float) count;
    }

    auto sorted = energy;
    std::nth_element (sorted.begin(), sorted.begin() + ringPoints / 2, sorted.end());
    const auto typical = sorted[(size_t) (ringPoints / 2)];
    const auto strongest = *std::max_element (energy.begin(), energy.end());
    const auto framePeak = *std::max_element (raw.begin(), raw.end());

    // Pico reciente (baja poco a poco): una parte más baja de la canción se
    // ve más tranquila, y el silencio deja el círculo liso.
    ringPeak = juce::jmax (framePeak, 0.02f, ringPeak * (float) std::exp (-seconds / 1.5));
    const auto loudness = juce::jlimit (0.0f, 1.0f, framePeak / ringPeak);

    // Subida inmediata y bajada rápida. Los extremos del anillo (abajo, donde
    // se unen) se atenúan para que no haya un salto.
    const auto release = (float) std::exp (-seconds / 0.06);
    const auto calm = finished && succeeded ? 0.35f : 1.0f;

    for (int k = 0; k < ringPoints; ++k)
    {
        const auto seam = std::pow (std::sin (juce::MathConstants<float>::pi * ((float) k + 0.5f) / (float) ringPoints), 0.8f);
        const auto region = strongest > typical ? juce::jlimit (0.0f, 1.0f, (energy[(size_t) k] - typical) / (strongest - typical)) : 0.0f;
        const auto detail = framePeak > 0.0f ? raw[(size_t) k] / framePeak : 0.0f;
        const auto target = calm * seam * juce::jmin (1.0f, 1.8f * std::sqrt (loudness) * std::pow (region, 1.1f) * (0.25f + 0.75f * detail));
        auto& level = ringLevels[(size_t) k];
        level = juce::jmax (target, level * release);
    }
}

void SeparationView::resized()
{
    auto footer = getLocalBounds().removeFromBottom (footerHeight);
    cancelButton.setBounds (footer.withSizeKeepingCentre (190, 30));
}

//==============================================================================
void SeparationView::paint (juce::Graphics& g)
{
    g.fillAll (DarkPalette::background);

    auto bounds = getLocalBounds().toFloat();
    auto header = bounds.removeFromTop ((float) headerHeight).reduced (20.0f, 10.0f);
    bounds.removeFromBottom ((float) footerHeight);

    // Título y estado.
    g.setColour (DarkPalette::text);
    g.setFont (juce::FontOptions (19.0f, juce::Font::bold));
    g.drawText (finished && succeeded ? tr ("Pistas de \"{0}\"", sourceName) : tr ("Separando \"{0}\"", sourceName),
                header.removeFromTop (26.0f), juce::Justification::centred, true);
    g.setColour (DarkPalette::textDim);
    g.setFont (juce::FontOptions (13.5f));
    g.drawText (status, header, juce::Justification::centred, true);

    const auto centre = bounds.getCentre();
    const auto unit = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto mainRadius = unit * 0.13f;
    const auto orbit = unit * 0.37f;
    const auto stemRadius = unit * 0.085f;
    const auto numStems = static_cast<int> (stems.size());

    // Posición y aparición de cada pista (sale del centro y se coloca en su sitio).
    struct Placed { juce::Point<float> position; float appear; };
    std::vector<Placed> placed;

    for (int i = 0; i < numStems; ++i)
    {
        const auto since = appearedAt[static_cast<size_t> (i)];
        const auto appear = since < 0.0 ? 0.0f : (float) juce::jlimit (0.0, 1.0, (time - since) / appearSeconds)
                                                 * presence[static_cast<size_t> (i)];
        const auto angle = -juce::MathConstants<float>::halfPi + twoPi * (float) i / (float) juce::jmax (1, numStems)
                         + (float) time * 0.12f;
        const juce::Point<float> slot (centre.x + orbit * std::cos (angle), centre.y + orbit * std::sin (angle));
        placed.push_back ({ centre + (slot - centre) * easeOutCubic (appear), appear });
    }

    // Detrás: los haces que unen el anillo con cada pista.
    for (int i = 0; i < numStems; ++i)
    {
        const auto& [position, appear] = placed[(size_t) i];
        const auto offset = position - centre;
        const auto distance = offset.getDistanceFromOrigin();

        // Del anillo al borde de la onda: no cruza el porcentaje.
        if (appear > 0.0f && distance > mainRadius * 1.32f + stemRadius)
            drawBeam (g, centre + offset * (mainRadius * 1.32f / distance),
                      position - offset * (stemRadius * 0.95f / distance), stems[(size_t) i].colour, appear, i);
    }

    drawCentre (g, centre, mainRadius);

    for (int i = 0; i < numStems; ++i)
    {
        const auto& [position, appear] = placed[(size_t) i];

        if (appear <= 0.0f)
            continue;

        drawStemOrb (g, i, position, stemRadius * easeOutBack (appear), juce::jmin (1.0f, appear * 1.5f));
    }
}

void SeparationView::drawCentre (juce::Graphics& g, juce::Point<float> centre, float radius) const
{
    fillGlow (g, centre, radius * 2.3f, sourceColour, 0.18f);
    drawFrequencyRing (g, centre, radius);

    // En el centro, el porcentaje. Mientras no hay porcentaje (cargando el
    // modelo), unos puntos que se van completando.
    juce::String text;

    if (progress < 0.0 && ! finished)
        text = juce::String::repeatedString (".", 1 + static_cast<int> (std::fmod (time * 2.5, 3.0)));
    else
        text = juce::String (juce::roundToInt (shownProgress * 100.0)) + "%";

    const auto area = juce::Rectangle<float> (radius * 2.4f, radius * 1.2f).withCentre (centre);
    g.setFont (juce::FontOptions (radius * 0.7f, juce::Font::bold));

    // Resplandor del color de la pista detrás del número, y el número en blanco.
    for (const auto offset : { 3.0f, 1.5f })
    {
        g.setColour (sourceColour.withAlpha (0.35f));
        g.drawText (text, area.translated (-offset, 0.0f), juce::Justification::centred, false);
        g.drawText (text, area.translated (offset, 0.0f), juce::Justification::centred, false);
        g.drawText (text, area.translated (0.0f, -offset), juce::Justification::centred, false);
        g.drawText (text, area.translated (0.0f, offset), juce::Justification::centred, false);
    }

    g.setColour (juce::Colours::white);
    g.drawText (text, area, juce::Justification::centred, false);
}

void SeparationView::drawFrequencyRing (juce::Graphics& g, juce::Point<float> centre, float radius) const
{
    // Una línea cerrada: el círculo base más el nivel de cada punto hacia fuera.
    // Empieza y termina abajo (donde los niveles se atenúan) y gira despacio.
    const auto base = radius * 1.32f;
    const auto reach = radius * 0.72f;
    const auto rotation = juce::MathConstants<float>::halfPi + (float) time * 0.25f;

    juce::Path ring;
    std::vector<juce::Point<float>> points ((size_t) ringPoints);

    for (int k = 0; k < ringPoints; ++k)
    {
        const auto angle = rotation + twoPi * (float) k / (float) ringPoints;
        const auto r = base + reach * ringLevels[(size_t) k];
        points[(size_t) k] = { centre.x + r * std::cos (angle), centre.y + r * std::sin (angle) };

        if (k == 0)
            ring.startNewSubPath (points[(size_t) k]);
        else
            ring.lineTo (points[(size_t) k]);
    }

    ring.closeSubPath();

    // Resplandor del color de la pista y, encima, la línea blanca fina.
    g.setColour (sourceColour.withAlpha (0.16f));
    g.strokePath (ring, juce::PathStrokeType (8.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (sourceColour.brighter (0.5f).withAlpha (0.45f));
    g.strokePath (ring, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (juce::Colours::white.withAlpha (0.95f));
    g.strokePath (ring, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Puntos brillantes en los picos: máximos locales altos; el mayor, más grande.
    const auto dotColour = sourceColour.brighter (0.9f);
    int highest = -1;

    for (int k = 0; k < ringPoints; ++k)
    {
        const auto level = ringLevels[(size_t) k];

        if (highest < 0 || level > ringLevels[(size_t) highest])
            highest = k;

        const auto previous = ringLevels[(size_t) ((k + ringPoints - 3) % ringPoints)];
        const auto next = ringLevels[(size_t) ((k + 3) % ringPoints)];

        if (level < 0.22f || level < previous || level < next || k % 2 != 0)
            continue;

        fillGlow (g, points[(size_t) k], 4.0f + 8.0f * level, dotColour, 0.55f * level);
        g.setColour (dotColour.withAlpha (0.6f + 0.4f * level));
        g.fillEllipse (juce::Rectangle<float> (2.0f + 2.5f * level, 2.0f + 2.5f * level).withCentre (points[(size_t) k]));
    }

    if (highest >= 0 && ringLevels[(size_t) highest] > 0.2f)
    {
        const auto& peak = points[(size_t) highest];
        fillGlow (g, peak, 18.0f, dotColour, 0.75f);
        g.setColour (juce::Colours::white);
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (peak));
    }
}

void SeparationView::drawBeam (juce::Graphics& g, juce::Point<float> from, juce::Point<float> to,
                               juce::Colour colour, float alpha, int index) const
{
    g.setColour (colour.withAlpha (0.22f * alpha));
    g.drawLine (juce::Line<float> (from, to), 1.5f);

    // Partículas que viajan de la canción hacia la pista separada.
    for (int p = 0; p < 3; ++p)
    {
        const auto f = (float) std::fmod (time * 0.55 + p / 3.0 + index * 0.17, 1.0);
        const auto point = from + (to - from) * f;
        const auto size = 3.0f + 2.0f * std::sin (f * juce::MathConstants<float>::pi);
        g.setColour (colour.withAlpha (alpha * (0.35f + 0.65f * std::sin (f * juce::MathConstants<float>::pi))));
        g.fillEllipse (juce::Rectangle<float> (size, size).withCentre (point));
    }
}

void SeparationView::drawStemOrb (juce::Graphics& g, int index, juce::Point<float> centre, float radius, float alpha) const
{
    if (radius <= 0.5f)
        return;

    const auto colour = stems[(size_t) index].colour;
    const auto t = (float) time + (float) index * 0.7f;
    const auto circle = [&centre] (float r) { return juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre); };

    fillGlow (g, centre, radius * 1.9f, colour, 0.3f * alpha);

    // Cada pista tiene su propia animación.
    switch (index % 6)
    {
        case 0:     // ondas que se expanden
            for (int j = 0; j < 3; ++j)
            {
                const auto f = (float) std::fmod (t * 0.7f + (float) j / 3.0f, 1.0f);
                g.setColour (colour.withAlpha (alpha * (1.0f - f) * 0.9f));
                g.drawEllipse (circle (radius * (0.6f + 0.75f * f)), 1.8f);
            }
            break;

        case 1:     // lunas en órbita
            g.setColour (colour.withAlpha (0.35f * alpha));
            g.drawEllipse (circle (radius * 0.95f), 1.0f);
            for (int j = 0; j < 5; ++j)
            {
                const auto angle = t * 2.1f + twoPi * (float) j / 5.0f;
                const auto size = radius * (0.14f + 0.06f * std::sin (t * 3.0f + (float) j));
                g.setColour (colour.brighter (0.5f).withAlpha (alpha));
                g.fillEllipse (juce::Rectangle<float> (size * 2.0f, size * 2.0f)
                                   .withCentre (centre.translated (radius * 0.95f * std::cos (angle), radius * 0.95f * std::sin (angle))));
            }
            break;

        case 2:     // arcos que giran en sentidos contrarios
        {
            juce::Path arcs;
            arcs.addCentredArc (centre.x, centre.y, radius * 0.78f, radius * 0.78f, t * 2.4f, 0.0f, 3.8f, true);
            arcs.addCentredArc (centre.x, centre.y, radius * 0.98f, radius * 0.98f, -t * 1.7f, 0.0f, 3.2f, true);
            g.setColour (colour.brighter (0.3f).withAlpha (alpha));
            g.strokePath (arcs, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            break;
        }

        case 3:     // oscilador: el borde vibra como una onda
        {
            juce::Path blob;

            for (int k = 0; k <= 90; ++k)
            {
                const auto theta = twoPi * (float) k / 90.0f;
                const auto r = radius * (0.78f + 0.14f * std::sin (6.0f * theta + t * 5.0f) + 0.06f * std::sin (11.0f * theta - t * 3.0f));
                const juce::Point<float> point (centre.x + r * std::cos (theta + t * 0.8f), centre.y + r * std::sin (theta + t * 0.8f));

                if (k == 0)
                    blob.startNewSubPath (point);
                else
                    blob.lineTo (point);
            }

            blob.closeSubPath();
            g.setColour (colour.withAlpha (0.3f * alpha));
            g.fillPath (blob);
            g.setColour (colour.brighter (0.4f).withAlpha (alpha));
            g.strokePath (blob, juce::PathStrokeType (1.8f));
            break;
        }

        case 4:     // ecualizador circular
            for (int k = 0; k < 28; ++k)
            {
                const auto theta = twoPi * (float) k / 28.0f + t * 0.6f;
                const auto level = 0.2f + 0.8f * std::abs (std::sin (t * 4.0f + (float) k * 0.9f));
                const juce::Point<float> direction (std::cos (theta), std::sin (theta));
                g.setColour (colour.brighter (0.3f).withAlpha (alpha * (0.4f + 0.6f * level)));
                g.drawLine (juce::Line<float> (centre + direction * radius * 0.66f,
                                               centre + direction * radius * (0.66f + 0.42f * level)), 2.0f);
            }
            break;

        default:    // pétalos que giran
        {
            juce::Path petals;

            for (int k = 0; k <= 120; ++k)
            {
                const auto theta = twoPi * (float) k / 120.0f;
                const auto r = radius * (0.35f + 0.65f * std::abs (std::cos (3.0f * theta)));
                const juce::Point<float> point (centre.x + r * std::cos (theta + t * 1.4f), centre.y + r * std::sin (theta + t * 1.4f));

                if (k == 0)
                    petals.startNewSubPath (point);
                else
                    petals.lineTo (point);
            }

            petals.closeSubPath();
            g.setColour (colour.withAlpha (0.35f * alpha));
            g.fillPath (petals);
            g.setColour (colour.brighter (0.4f).withAlpha (alpha));
            g.strokePath (petals, juce::PathStrokeType (1.5f));
            break;
        }
    }
}

//==============================================================================
SeparationWindow::SeparationWindow (const juce::String& sourceName, juce::Colour sourceColour,
                                    std::vector<SeparationView::Stem> stems)
    : juce::DocumentWindow (tr ("Separando instrumentos"), DarkPalette::background, juce::DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (true);
    view = new SeparationView (sourceName, sourceColour, std::move (stems));
    setContentOwned (view, true);
    setResizable (true, false);
    setResizeLimits (420, 420, 1400, 1300);
    centreWithSize (getWidth(), getHeight());
}

void SeparationWindow::present()
{
    setVisible (true);
    toFront (true);

    if (onVisibilityChanged != nullptr)
        onVisibilityChanged();
}

void SeparationWindow::closeButtonPressed()
{
    setVisible (false);

    if (onVisibilityChanged != nullptr)
        onVisibilityChanged();
}
}
