#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "StemLabLookAndFeel.h"
#include "Utils/Localisation.h"

#include <functional>
#include <vector>

namespace stemlab
{
/**
    Tutorial: recorre la ventana parte por parte. Oscurece todo, deja a la
    vista la zona de la que se habla (con un borde que late) y pone al lado
    una tarjeta, con una flecha que la señala, que explica qué hace.

    Aparece cada vez que se abre StemLab sin un proyecto (se cierra con
    "Saltar tutorial" o Esc) y desde Ayuda > Tutorial. Siguiente: → o Intro. Atrás: ←. Salir: Esc.

    Ocupa toda la ventana principal y se queda con el ratón y el teclado: los
    atajos de la aplicación no actúan mientras está abierto.
*/
class TourOverlay final : public juce::Component,
                          private juce::Timer
{
public:
    struct Step
    {
        /** La zona que se ilumina, en coordenadas de la ventana; vacía o sin
            función = ninguna (tarjeta en el centro). */
        std::function<juce::Rectangle<int>()> target;
        juce::String title;
        juce::String body;
        /** El primer paso: también deja elegir idioma y tema. */
        bool settings = false;
    };

    explicit TourOverlay (std::vector<Step> steps);

    /** Se cerró (terminó, "Saltar tutorial" o Esc). Quien lo creó lo destruye. */
    std::function<void()> onClose;

    /** Se eligió otro idioma u otro tema en el primer paso. La ventana se
        vuelve a crear con ellos (y abre de nuevo el tutorial). */
    std::function<void (Language)> onLanguageChosen;
    std::function<void (Theme)> onThemeChosen;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override    {}      // nada de lo de debajo recibe el clic

    int getStepIndex() const noexcept       { return index; }
    int getNumSteps() const noexcept        { return static_cast<int> (steps.size()); }
    juce::Rectangle<int> getCardBounds() const noexcept      { return card; }
    juce::Rectangle<int> getSpotlightBounds() const noexcept { return spotlight; }

    void next();
    void back();
    void close();

private:
    enum class Side { none, top, bottom, left, right };      // el lado de la tarjeta por el que sale la flecha

    void showStep (int newIndex);
    void layout();
    juce::AttributedString bodyText() const;
    void timerCallback() override;

    std::vector<Step> steps;
    int index = 0;

    juce::Rectangle<int> spotlight;      // vacío: sin zona iluminada
    juce::Rectangle<int> card;
    juce::Rectangle<int> textArea;       // dentro de la tarjeta
    Side arrowSide = Side::none;
    juce::Point<float> arrowTip;
    double pulse = 0.0;

    juce::TextButton skipButton, backButton, nextButton;
    juce::Label languageLabel, themeLabel;
    juce::OwnedArray<juce::TextButton> languageButtons;
    juce::TextButton darkButton, lightButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TourOverlay)
};
}
