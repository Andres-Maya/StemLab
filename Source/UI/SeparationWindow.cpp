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
    constexpr double appearSeconds = 0.8;       // lo que tarda una esfera nueva en salir y colocarse
    constexpr int headerHeight = 64;
    constexpr int footerHeight = 58;

    float easeOutCubic (float x)                { x = 1.0f - juce::jlimit (0.0f, 1.0f, x); return 1.0f - x * x * x; }

    float easeOutBack (float x)
    {
        // Se pasa un poco y vuelve: la esfera "rebota" al llegar a su sitio.
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
      appearedAt (stems.size(), -1.0)
{
    setOpaque (true);

    cancelButton.setButtonText ("Cancelar separación"_u8);
    cancelButton.onClick = [this] { if (onCancel != nullptr) onCancel(); };
    addAndMakeVisible (cancelButton);

    setSize (620, 580);
}

double SeparationView::appearanceThreshold (int index, int numStems)
{
    return static_cast<double> (index + 1) / static_cast<double> (numStems + 1);
}

int SeparationView::getNumVisibleStems() const noexcept
{
    return static_cast<int> (std::count_if (appearedAt.begin(), appearedAt.end(), [] (double t) { return t >= 0.0; }));
}

void SeparationView::setFinished (bool didSucceed)
{
    finished = true;
    succeeded = didSucceed;
    cancelButton.setVisible (false);
    advance (0.0);
}

void SeparationView::visibilityChanged()
{
    // Solo se anima mientras se ve.
    if (isShowing())
    {
        lastFrameMs = juce::Time::getMillisecondCounterHiRes();
        startTimerHz (60);
    }
    else
    {
        stopTimer();
    }
}

void SeparationView::timerCallback()
{
    if (! isShowing())
    {
        stopTimer();
        return;
    }

    const auto now = juce::Time::getMillisecondCounterHiRes();
    advance (juce::jlimit (0.0, 0.1, (now - lastFrameMs) / 1000.0));
    lastFrameMs = now;
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
        status = "¡Separación completada!"_u8;
    }

    // El número se acerca al progreso real sin saltos.
    if (progress >= 0.0)
        shownProgress += (progress - shownProgress) * (1.0 - std::exp (-seconds / 0.25));

    // Cada pista aparece al pasar su porcentaje (todas al terminar bien).
    const auto numStems = static_cast<int> (stems.size());

    for (int i = 0; i < numStems; ++i)
        if (appearedAt[static_cast<size_t> (i)] < 0.0 && progress >= appearanceThreshold (i, numStems))
            appearedAt[static_cast<size_t> (i)] = time;

    repaint();
}

void SeparationView::resized()
{
    auto footer = getLocalBounds().removeFromBottom (footerHeight);
    cancelButton.setBounds (footer.withSizeKeepingCentre (190, 30));
}

//==============================================================================
void SeparationView::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background);

    auto bounds = getLocalBounds().toFloat();
    auto header = bounds.removeFromTop ((float) headerHeight).reduced (20.0f, 10.0f);
    bounds.removeFromBottom ((float) footerHeight);

    // Título y estado.
    g.setColour (Palette::text);
    g.setFont (juce::FontOptions (19.0f, juce::Font::bold));
    g.drawText ("Separando \"" + sourceName + "\"", header.removeFromTop (26.0f), juce::Justification::centred, true);
    g.setColour (Palette::textDim);
    g.setFont (juce::FontOptions (13.5f));
    g.drawText (status, header, juce::Justification::centred, true);

    const auto centre = bounds.getCentre();
    const auto unit = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto mainRadius = unit * 0.16f;
    const auto orbit = unit * 0.37f;
    const auto stemRadius = unit * 0.085f;
    const auto numStems = static_cast<int> (stems.size());

    // Posición y aparición de cada pista (sale del centro y se coloca en su sitio).
    struct Placed { juce::Point<float> position; float appear; };
    std::vector<Placed> placed;

    for (int i = 0; i < numStems; ++i)
    {
        const auto since = appearedAt[static_cast<size_t> (i)];
        const auto appear = since < 0.0 ? 0.0f : (float) juce::jlimit (0.0, 1.0, (time - since) / appearSeconds);
        const auto angle = -juce::MathConstants<float>::halfPi + twoPi * (float) i / (float) juce::jmax (1, numStems)
                         + (float) time * 0.12f;
        const juce::Point<float> slot (centre.x + orbit * std::cos (angle), centre.y + orbit * std::sin (angle));
        placed.push_back ({ centre + (slot - centre) * easeOutCubic (appear), appear });
    }

    // Detrás: los haces que unen la esfera central con cada pista.
    for (int i = 0; i < numStems; ++i)
        if (placed[(size_t) i].appear > 0.0f)
            drawBeam (g, centre, placed[(size_t) i].position, stems[(size_t) i].colour, placed[(size_t) i].appear, i);

    drawMainOrb (g, centre, mainRadius);

    for (int i = 0; i < numStems; ++i)
    {
        const auto& [position, appear] = placed[(size_t) i];

        if (appear <= 0.0f)
            continue;

        drawStemOrb (g, i, position, stemRadius * easeOutBack (appear), juce::jmin (1.0f, appear * 1.5f));

        g.setColour (Palette::text.withAlpha (appear));
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (stems[(size_t) i].name,
                    juce::Rectangle<float> (120.0f, 18.0f).withCentre (position.translated (0.0f, stemRadius * 1.55f + 10.0f)),
                    juce::Justification::centred, false);
    }
}

void SeparationView::drawMainOrb (juce::Graphics& g, juce::Point<float> centre, float radius) const
{
    const auto t = (float) time;
    const auto rotation = t * 0.9f;

    fillGlow (g, centre, radius * 2.1f, sourceColour, 0.28f);

    // Anillo de frecuencias: gira con la esfera y cada barra sube y baja.
    constexpr int bars = 84;
    const auto energy = finished && succeeded ? 0.5f : 1.0f;

    for (int k = 0; k < bars; ++k)
    {
        const auto theta = twoPi * (float) k / (float) bars;
        const auto level = std::abs (0.55f * std::sin (3.0f * theta + 2.3f * t)
                                     + 0.30f * std::sin (7.0f * theta - 3.1f * t + std::sin (t))
                                     + 0.15f * std::sin (13.0f * theta + 5.7f * t));
        const auto amount = energy * (0.12f + 0.88f * level);
        const auto angle = theta + rotation;
        const auto inner = radius * 1.1f;
        const auto outer = inner + radius * 0.55f * amount;
        const juce::Point<float> direction (std::cos (angle), std::sin (angle));

        g.setColour (sourceColour.brighter (0.4f).withAlpha (0.35f + 0.65f * amount));
        g.drawLine (juce::Line<float> (centre + direction * inner, centre + direction * outer), 2.4f);
    }

    fillSphere (g, centre, radius, sourceColour, 1.0f);

    // Meridianos: elipses que se estrechan y ensanchan dan la sensación de giro.
    juce::Graphics::ScopedSaveState clip (g);
    juce::Path sphere;
    sphere.addEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));
    g.reduceClipRegion (sphere);

    g.setColour (juce::Colours::white.withAlpha (0.16f));

    for (int m = 0; m < 4; ++m)
    {
        const auto phase = t * 1.3f + (float) m * juce::MathConstants<float>::pi / 4.0f;
        const auto halfWidth = radius * std::abs (std::cos (phase));
        g.drawEllipse (juce::Rectangle<float> (halfWidth * 2.0f, radius * 2.0f).withCentre (centre), 1.2f);
    }

    g.drawEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 0.55f).withCentre (centre), 1.2f);

    // Onda que recorre la esfera por dentro.
    juce::Path wave;

    for (int i = 0; i <= 60; ++i)
    {
        const auto x = -1.0f + 2.0f * (float) i / 60.0f;
        const auto envelope = 1.0f - x * x;
        const auto y = envelope * (0.22f * std::sin (x * 9.0f + t * 5.0f) + 0.12f * std::sin (x * 23.0f - t * 7.0f));
        const juce::Point<float> point (centre.x + x * radius, centre.y + y * radius);

        if (i == 0)
            wave.startNewSubPath (point);
        else
            wave.lineTo (point);
    }

    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.strokePath (wave, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Brillo.
    g.setColour (juce::Colours::white.withAlpha (0.22f));
    g.fillEllipse (juce::Rectangle<float> (radius * 0.7f, radius * 0.4f).withCentre (centre.translated (-radius * 0.35f, -radius * 0.55f)));

    // Porcentaje, con sombra para que se lea sobre la onda.
    g.setFont (juce::FontOptions (radius * 0.42f, juce::Font::bold));
    const auto text = progress < 0.0 && ! finished ? juce::String ("...") : juce::String (juce::roundToInt (shownProgress * 100.0)) + " %";
    const auto textArea = juce::Rectangle<float> (radius * 2.0f, radius).withCentre (centre);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.drawText (text, textArea.translated (1.5f, 2.0f), juce::Justification::centred, false);
    g.setColour (juce::Colours::white);
    g.drawText (text, textArea, juce::Justification::centred, false);
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
            fillSphere (g, centre, radius * 0.62f, colour, alpha);
            break;

        case 1:     // lunas en órbita
            fillSphere (g, centre, radius * 0.58f, colour, alpha);
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
            fillSphere (g, centre, radius * 0.5f, colour, alpha);
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
            fillSphere (g, centre, radius * 0.5f, colour, alpha);
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
            fillSphere (g, centre, radius * 0.58f, colour, alpha);
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
            fillSphere (g, centre, radius * 0.42f, colour, alpha);
            break;
        }
    }
}

//==============================================================================
SeparationWindow::SeparationWindow (const juce::String& sourceName, juce::Colour sourceColour,
                                    std::vector<SeparationView::Stem> stems)
    : juce::DocumentWindow ("Separando instrumentos", Palette::background, juce::DocumentWindow::closeButton)
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
}
}
