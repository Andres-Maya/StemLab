#include "TourOverlay.h"

#include "Utils/Strings.h"

namespace stemlab
{
namespace
{
    constexpr int cardWidth = 400;
    constexpr int cardPadding = 16;
    constexpr int margin = 12;          // de la tarjeta a los bordes de la ventana
    constexpr int gap = 16;             // de la tarjeta a la zona iluminada
    constexpr int spotlightPadding = 5;
    constexpr int buttonHeight = 30;
    constexpr int optionHeight = 28;
    constexpr int countHeight = 16;
    constexpr int titleHeight = 26;
    constexpr float arrowSize = 9.0f;

    void styleOption (juce::TextButton& button, bool selected)
    {
        button.setToggleState (selected, juce::dontSendNotification);
        button.setColour (juce::TextButton::buttonColourId, Palette::panelLight);
        button.setColour (juce::TextButton::buttonOnColourId, Palette::accent);
        button.setColour (juce::TextButton::textColourOffId, Palette::text);
        button.setColour (juce::TextButton::textColourOnId, Palette::background);
    }
}

TourOverlay::TourOverlay (std::vector<Step> tourSteps)
    : steps (std::move (tourSteps))
{
    jassert (! steps.empty());

    setWantsKeyboardFocus (true);
    setInterceptsMouseClicks (true, true);

    skipButton.setButtonText (tr ("Saltar tutorial"));
    skipButton.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    skipButton.setColour (juce::TextButton::textColourOffId, Palette::textDim);
    skipButton.setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    skipButton.onClick = [this] { close(); };

    backButton.setButtonText (tr ("Atrás"));
    backButton.onClick = [this] { back(); };

    nextButton.setColour (juce::TextButton::buttonColourId, Palette::accent);
    nextButton.setColour (juce::TextButton::textColourOffId, Palette::background);
    nextButton.onClick = [this] { next(); };

    for (auto* button : { &skipButton, &backButton, &nextButton })
    {
        button->setWantsKeyboardFocus (false);      // el teclado es del tutorial (→, ←, Intro, Esc)
        addAndMakeVisible (*button);
    }

    // Primer paso: idioma y tema.
    for (auto* label : { &languageLabel, &themeLabel })
    {
        label->setFont (juce::FontOptions (14.0f));
        label->setColour (juce::Label::textColourId, Palette::textDim);
        addChildComponent (*label);
    }

    languageLabel.setText (tr ("Idioma"), juce::dontSendNotification);
    themeLabel.setText (tr ("Tema"), juce::dontSendNotification);

    for (const auto& info : Localisation::getLanguages())
    {
        auto* button = languageButtons.add (new juce::TextButton (juce::String::fromUTF8 (info.name)));
        styleOption (*button, info.language == Localisation::getLanguage());
        button->setWantsKeyboardFocus (false);
        button->onClick = [this, language = info.language]
        {
            if (language != Localisation::getLanguage() && onLanguageChosen != nullptr)
                onLanguageChosen (language);
        };
        addChildComponent (*button);
    }

    darkButton.setButtonText (tr ("Oscuro"));
    lightButton.setButtonText (tr ("Claro"));
    styleOption (darkButton, Palette::getTheme() == Theme::dark);
    styleOption (lightButton, Palette::getTheme() == Theme::light);

    const auto chooseTheme = [this] (Theme theme)
    {
        if (theme != Palette::getTheme() && onThemeChosen != nullptr)
            onThemeChosen (theme);
    };

    darkButton.onClick = [chooseTheme] { chooseTheme (Theme::dark); };
    lightButton.onClick = [chooseTheme] { chooseTheme (Theme::light); };

    for (auto* button : { &darkButton, &lightButton })
    {
        button->setWantsKeyboardFocus (false);
        addChildComponent (*button);
    }

    showStep (0);
    startTimerHz (30);
}

//==============================================================================
void TourOverlay::next()
{
    if (index + 1 >= getNumSteps())
        close();
    else
        showStep (index + 1);
}

void TourOverlay::back()
{
    if (index > 0)
        showStep (index - 1);
}

void TourOverlay::close()
{
    stopTimer();

    if (onClose != nullptr)
        onClose();          // puede destruir este componente: nada después
}

void TourOverlay::showStep (int newIndex)
{
    index = juce::jlimit (0, getNumSteps() - 1, newIndex);
    const auto last = index == getNumSteps() - 1;
    const auto settings = steps[static_cast<size_t> (index)].settings;

    nextButton.setButtonText (last ? tr ("Empezar") : tr ("Siguiente"));
    backButton.setEnabled (index > 0);
    skipButton.setVisible (! last);

    languageLabel.setVisible (settings);
    themeLabel.setVisible (settings);
    darkButton.setVisible (settings);
    lightButton.setVisible (settings);

    for (auto* button : languageButtons)
        button->setVisible (settings);

    pulse = 0.0;
    layout();
    repaint();
}

juce::AttributedString TourOverlay::bodyText() const
{
    juce::AttributedString text;
    text.setWordWrap (juce::AttributedString::byWord);
    text.setLineSpacing (3.0f);
    text.append (steps[static_cast<size_t> (index)].body, juce::Font (juce::FontOptions (14.5f)), Palette::text);
    return text;
}

/** Coloca la tarjeta donde quepa: debajo de la zona iluminada, encima, a la
    derecha o a la izquierda (en ese orden); si no cabe fuera, dentro. */
void TourOverlay::layout()
{
    const auto bounds = getLocalBounds();

    if (bounds.isEmpty())
        return;

    const auto& step = steps[static_cast<size_t> (index)];
    const auto width = juce::jmin (cardWidth, bounds.getWidth() - 2 * margin);
    const auto textWidth = width - 2 * cardPadding;

    juce::TextLayout bodyLayout;
    bodyLayout.createLayout (bodyText(), static_cast<float> (textWidth));
    const auto bodyHeight = juce::roundToInt (std::ceil (bodyLayout.getHeight())) + 4;

    const auto settingsHeight = step.settings ? 2 * (optionHeight + 8) + 4 : 0;
    const auto height = cardPadding + countHeight + titleHeight + 6 + bodyHeight + settingsHeight + 12 + buttonHeight + cardPadding - 2;

    const auto target = step.target != nullptr ? step.target().getIntersection (bounds) : juce::Rectangle<int>();
    spotlight = target.getWidth() > 2 && target.getHeight() > 2 ? target.expanded (spotlightPadding).getIntersection (bounds.reduced (2))
                                                                : juce::Rectangle<int>();

    const auto clampX = [&] (int x) { return juce::jlimit (margin, juce::jmax (margin, bounds.getWidth() - width - margin), x); };
    const auto clampY = [&] (int y) { return juce::jlimit (margin, juce::jmax (margin, bounds.getHeight() - height - margin), y); };

    arrowSide = Side::none;

    if (spotlight.isEmpty())
    {
        card = juce::Rectangle<int> (width, height).withCentre (bounds.getCentre());
    }
    else if (spotlight.getBottom() + gap + height + margin <= bounds.getHeight())
    {
        arrowSide = Side::top;
        card = { clampX (spotlight.getCentreX() - width / 2), spotlight.getBottom() + gap, width, height };
    }
    else if (spotlight.getY() - gap - height - margin >= 0)
    {
        arrowSide = Side::bottom;
        card = { clampX (spotlight.getCentreX() - width / 2), spotlight.getY() - gap - height, width, height };
    }
    else if (spotlight.getRight() + gap + width + margin <= bounds.getWidth())
    {
        arrowSide = Side::left;
        card = { spotlight.getRight() + gap, clampY (spotlight.getCentreY() - height / 2), width, height };
    }
    else if (spotlight.getX() - gap - width - margin >= 0)
    {
        arrowSide = Side::right;
        card = { spotlight.getX() - gap - width, clampY (spotlight.getCentreY() - height / 2), width, height };
    }
    else
    {
        // Una zona que ocupa casi toda la ventana: la tarjeta, dentro y abajo.
        card = { clampX (spotlight.getCentreX() - width / 2), clampY (spotlight.getBottom() - height - gap), width, height };
    }

    // La punta de la flecha, en el borde de la tarjeta que mira a la zona.
    const auto along = [] (int centre, int from, int to) { return static_cast<float> (juce::jlimit (from + 22, juce::jmax (from + 22, to - 22), centre)); };

    switch (arrowSide)
    {
        case Side::top:     arrowTip = { along (spotlight.getCentreX(), card.getX(), card.getRight()), (float) card.getY() - arrowSize }; break;
        case Side::bottom:  arrowTip = { along (spotlight.getCentreX(), card.getX(), card.getRight()), (float) card.getBottom() + arrowSize }; break;
        case Side::left:    arrowTip = { (float) card.getX() - arrowSize, along (spotlight.getCentreY(), card.getY(), card.getBottom()) }; break;
        case Side::right:   arrowTip = { (float) card.getRight() + arrowSize, along (spotlight.getCentreY(), card.getY(), card.getBottom()) }; break;
        case Side::none:    break;
    }

    // Dentro de la tarjeta: texto arriba; después idioma y tema; abajo, los botones.
    auto inner = card.reduced (cardPadding);
    auto buttons = inner.removeFromBottom (buttonHeight);
    nextButton.setBounds (buttons.removeFromRight (110));
    buttons.removeFromRight (8);
    backButton.setBounds (buttons.removeFromRight (80));
    skipButton.setBounds (buttons.removeFromLeft (130));
    inner.removeFromBottom (12);

    if (step.settings)
    {
        auto themeRow = inner.removeFromBottom (optionHeight);
        inner.removeFromBottom (8);
        auto languageRow = inner.removeFromBottom (optionHeight);
        inner.removeFromBottom (12);

        const auto optionWidth = 96;
        themeLabel.setBounds (themeRow.removeFromLeft (110));
        lightButton.setBounds (themeRow.removeFromRight (optionWidth));
        darkButton.setBounds (themeRow.removeFromRight (optionWidth));
        darkButton.setConnectedEdges (juce::Button::ConnectedOnRight);
        lightButton.setConnectedEdges (juce::Button::ConnectedOnLeft);

        languageLabel.setBounds (languageRow.removeFromLeft (110));

        for (int i = languageButtons.size(); --i >= 0;)
        {
            languageButtons[i]->setBounds (languageRow.removeFromRight (optionWidth));
            languageButtons[i]->setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0)
                                                   | (i + 1 < languageButtons.size() ? juce::Button::ConnectedOnRight : 0));
        }
    }

    textArea = inner;
}

void TourOverlay::resized()
{
    layout();
}

//==============================================================================
void TourOverlay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto& step = steps[static_cast<size_t> (index)];

    // Todo oscurecido menos la zona de la que se habla.
    juce::Path dim;
    dim.setUsingNonZeroWinding (false);
    dim.addRectangle (bounds);

    if (! spotlight.isEmpty())
        dim.addRoundedRectangle (spotlight.toFloat(), 8.0f);

    g.setColour (juce::Colours::black.withAlpha (0.62f));
    g.fillPath (dim);

    if (! spotlight.isEmpty())
    {
        g.setColour (Palette::accent);
        g.drawRoundedRectangle (spotlight.toFloat(), 8.0f, 2.0f);

        // Un segundo borde que se abre y se apaga: lleva la vista a la zona.
        const auto grow = static_cast<float> (pulse) * 9.0f;
        g.setColour (Palette::accent.withAlpha (0.85f * static_cast<float> (1.0 - pulse)));
        g.drawRoundedRectangle (spotlight.toFloat().expanded (grow), 8.0f + grow, 2.0f);
    }

    // La tarjeta y su flecha.
    juce::Path shape;
    shape.addRoundedRectangle (card.toFloat(), 10.0f);

    if (arrowSide != Side::none)
    {
        const auto horizontal = arrowSide == Side::top || arrowSide == Side::bottom;
        const auto base = arrowSide == Side::top ? (float) card.getY() + 1.0f
                        : arrowSide == Side::bottom ? (float) card.getBottom() - 1.0f
                        : arrowSide == Side::left ? (float) card.getX() + 1.0f
                        : (float) card.getRight() - 1.0f;

        if (horizontal)
            shape.addTriangle (arrowTip.x - arrowSize, base, arrowTip.x + arrowSize, base, arrowTip.x, arrowTip.y);
        else
            shape.addTriangle (base, arrowTip.y - arrowSize, base, arrowTip.y + arrowSize, arrowTip.x, arrowTip.y);
    }

    juce::DropShadow (juce::Colours::black.withAlpha (0.45f), 24, { 0, 8 }).drawForPath (g, shape);
    g.setColour (Palette::panel);
    g.fillPath (shape);
    g.setColour (Palette::accent);
    g.strokePath (shape, juce::PathStrokeType (1.2f));

    // Tapa el borde de la tarjeta en la base de la flecha: se ven como una sola figura.
    if (arrowSide != Side::none)
    {
        const auto inset = arrowSize - 1.6f;
        g.setColour (Palette::panel);

        if (arrowSide == Side::top || arrowSide == Side::bottom)
        {
            const auto y = arrowSide == Side::top ? (float) card.getY() : (float) card.getBottom();
            g.drawLine (arrowTip.x - inset, y, arrowTip.x + inset, y, 3.0f);
        }
        else
        {
            const auto x = arrowSide == Side::left ? (float) card.getX() : (float) card.getRight();
            g.drawLine (x, arrowTip.y - inset, x, arrowTip.y + inset, 3.0f);
        }
    }

    auto area = textArea;
    g.setColour (Palette::accent);
    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    g.drawText (tr ("Paso {0} de {1}", index + 1, getNumSteps()).toUpperCase(), area.removeFromTop (countHeight),
                juce::Justification::centredLeft, false);

    g.setColour (Palette::text);
    g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    g.drawFittedText (step.title, area.removeFromTop (titleHeight), juce::Justification::centredLeft, 1);
    area.removeFromTop (6);

    juce::TextLayout bodyLayout;
    bodyLayout.createLayout (bodyText(), static_cast<float> (area.getWidth()));
    bodyLayout.draw (g, area.toFloat());
}

void TourOverlay::timerCallback()
{
    if (spotlight.isEmpty())
        return;

    pulse = std::fmod (pulse + 1.0 / 48.0, 1.0);
    repaint (spotlight.expanded (14));
}

bool TourOverlay::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
        close();
    else if (key == juce::KeyPress::rightKey || key == juce::KeyPress::returnKey)
        next();
    else if (key == juce::KeyPress::leftKey)
        back();

    return true;        // los atajos de la aplicación no actúan detrás del tutorial
}
}
