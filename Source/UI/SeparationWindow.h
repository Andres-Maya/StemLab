#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Audio/AudioClip.h"

#include <functional>
#include <memory>
#include <vector>

namespace stemlab
{
/**
    Animación de la separación por IA:

      - En el centro, el porcentaje de la separación. Lo rodea un anillo de
        frecuencias del color de la pista que se separa: una línea que forma
        un círculo y se deforma con picos, con puntos brillantes en los más
        altos. Dibuja la propia canción: recorre su audio en tiempo real y lo
        pone en círculo (sin audio usa una señal sintética).
      - Cada cierto porcentaje sale del centro una de las pistas que se van a
        generar (Voz, Batería...): una animación de su color (ondas, órbita,
        arcos, oscilador, barras, pétalos) con su nombre dentro, que se coloca
        alrededor. Con 4 pistas aparecen al 20, 40, 60 y 80 %.
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

    /** Audio de la canción que se separa, para el anillo de frecuencias: el
        tramo [start, start + length) de source (el fragmento de la pista). */
    void setSourceAudio (std::shared_ptr<const ClipSource> source, juce::int64 start, juce::int64 length);

    /** Al terminar: si fue bien aparecen todas las pistas y el mensaje final. */
    void setFinished (bool succeeded);

    void advance (double seconds);

    /** Porcentaje al que aparece la pista index de numStems: (index + 1) / (numStems + 1). */
    static double appearanceThreshold (int index, int numStems);

    int getNumVisibleStems() const noexcept;
    double getAnimationTime() const noexcept        { return time; }

    /** Nivel (0..1) de cada punto del anillo de frecuencias, empezando por abajo. */
    const std::vector<float>& getRingLevels() const noexcept    { return ringLevels; }
    static constexpr int ringPoints = 360;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateRing (double seconds);

    void drawCentre (juce::Graphics&, juce::Point<float> centre, float radius) const;
    void drawFrequencyRing (juce::Graphics&, juce::Point<float> centre, float radius) const;
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

    std::shared_ptr<const ClipSource> audio;
    juce::int64 audioStart = 0, audioLength = 0;
    std::vector<float> ringLevels = std::vector<float> (ringPoints, 0.0f);
    float ringPeak = 0.05f;                         // pico reciente: el anillo se ve igual en canciones bajas o altas

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
