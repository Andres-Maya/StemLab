#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace stemlab
{
/** Tema de la interfaz: oscuro (el de siempre) o claro. */
enum class Theme { dark, light };

/** Colores del tema actual. Los neutros y el de acento cambian con
    Palette::setTheme(); después hay que volver a crear la interfaz (los
    componentes copian sus colores al construirse): lo hace MainWindow. */
namespace Palette
{
    inline juce::Colour background { 0xff121419 };
    inline juce::Colour panel      { 0xff1b1e25 };
    inline juce::Colour panelLight { 0xff252932 };
    inline juce::Colour outline    { 0xff343945 };
    inline juce::Colour text       { 0xffe4e6eb };
    inline juce::Colour textDim    { 0xff8b919c };
    inline juce::Colour accent     { 0xff4fc3f7 };

    inline juce::Colour highlight  { 0xffffffff };     // lo más brillante de la ventana de ondas: blanco / casi negro
    inline juce::Colour mute       { 0xfff0b429 };
    inline juce::Colour solo       { 0xff5ccb7a };

    // Iguales en los dos temas.
    inline const juce::Colour record     { 0xffe5484d };
    inline const juce::Colour onText     { 0xff121419 };     // texto sobre un botón activado (M, S, Ondas)

    Theme getTheme();
    void setTheme (Theme theme);

    /** El color de una pista tal como se pinta sobre el fondo del tema: los
        tonos pastel del tema oscuro apenas se ven sobre un fondo claro, así
        que se oscurecen. */
    juce::Colour onBackground (juce::Colour colour);

    /** Un tono que destaca sobre el propio color (los brillos de la ventana
        de ondas): más claro en el tema oscuro y más oscuro en el claro, donde
        un tono más claro se perdería contra el fondo. */
    juce::Colour emphasised (juce::Colour colour, float amount);
}

/** Los colores del tema oscuro, fijos (referencia para las pruebas). */
namespace DarkPalette
{
    inline const juce::Colour background { 0xff121419 };
    inline const juce::Colour panelLight { 0xff252932 };
    inline const juce::Colour outline    { 0xff343945 };
    inline const juce::Colour text       { 0xffe4e6eb };
    inline const juce::Colour textDim    { 0xff8b919c };
}

/** Color de una pista según su nombre (stems conocidos) o su posición. */
juce::Colour trackColourFor (const juce::String& trackName, int index);

/** Aspecto de StemLab, con los colores del tema actual. */
class StemLabLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    StemLabLookAndFeel();

    /** Vuelve a tomar los colores de Palette (después de Palette::setTheme). */
    void applyTheme();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
};
}
