#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace stemlab
{
/**
    Animación de la separación por IA:

      - En el centro, una esfera del color de la pista que se separa. Gira y a
        su alrededor late un anillo de barras de frecuencia.
      - Cada cierto porcentaje sale de ella otra esfera, del color de una de
        las pistas que se van a generar (Voz, Batería...). Se coloca alrededor
        y cada una tiene su propia animación (ondas, órbita, arcos, oscilador,
        barras, pétalos). Con 4 pistas aparecen al 20, 40, 60 y 80 %.
      - Al terminar aparecen todas y se muestra "Separación completada".

    El progreso y el estado se leen con getProgress / getStatus (60 veces por
    segundo mientras se ve). advance() hace avanzar la animación: el timer lo
    llama con el tiempo real y las pruebas, a mano.
*/
class SeparationView final : public juce::Component,
                             private juce::Timer
{
public:
    struct Stem
    {
        juce::String name;      // nombre visible ("Voz")
        juce::Colour colour;    // el que tendrá la pista
    };

    SeparationView (juce::String sourceName, juce::Colour sourceColour, std::vector<Stem> stems);

    std::function<double()> getProgress;            // 0..1, negativo = todavía sin porcentaje
    std::function<juce::String()> getStatus;
    std::function<void()> onCancel;

    /** Al terminar: si fue bien aparecen todas las pistas y el mensaje final. */
    void setFinished (bool succeeded);

    void advance (double seconds);

    /** Porcentaje al que aparece la pista index de numStems: (index + 1) / (numStems + 1). */
    static double appearanceThreshold (int index, int numStems);

    int getNumVisibleStems() const noexcept;
    double getAnimationTime() const noexcept        { return time; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

private:
    void timerCallback() override;

    void drawMainOrb (juce::Graphics&, juce::Point<float> centre, float radius) const;
    void drawStemOrb (juce::Graphics&, int index, juce::Point<float> centre, float radius, float alpha) const;
    void drawBeam (juce::Graphics&, juce::Point<float> from, juce::Point<float> to, juce::Colour, float alpha, int index) const;

    juce::String sourceName;
    juce::Colour sourceColour;
    std::vector<Stem> stems;
    std::vector<double> appearedAt;                 // segundo de la animación en que apareció (-1: aún no)

    double time = 0.0;
    double progress = -1.0;
    double shownProgress = 0.0;                     // el porcentaje que se ve, suavizado
    bool finished = false;
    bool succeeded = false;
    juce::String status;
    double lastFrameMs = 0.0;

    juce::TextButton cancelButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SeparationView)
};

/**
    Ventana propia de la separación. Cerrarla solo la oculta: la separación
    sigue y se puede volver a abrir (barra de estado > Ver progreso, o IA >
    Mostrar progreso de la separación).
*/
class SeparationWindow final : public juce::DocumentWindow
{
public:
    SeparationWindow (const juce::String& sourceName, juce::Colour sourceColour, std::vector<SeparationView::Stem> stems);

    SeparationView& getView() noexcept      { return *view; }

    /** Mostrar y traer al frente. */
    void present();

    void closeButtonPressed() override      { setVisible (false); }

private:
    SeparationView* view = nullptr;         // propiedad de la ventana (setContentOwned)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SeparationWindow)
};
}
