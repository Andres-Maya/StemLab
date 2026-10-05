#include "EffectPanel.h"

#include "StemLabLookAndFeel.h"
#include "Utils/Strings.h"

namespace stemlab
{
namespace
{
    constexpr int headerHeight = 28;
    constexpr int columnWidth = 68;
    constexpr int rowHeight = 78;
    constexpr int labelHeight = 14;
    constexpr int padding = 8;
}

EffectPanel::EffectPanel (AudioEffect& audioEffect)
    : effect (audioEffect)
{
    enableButton.setTooltip (tr ("Activar / desactivar"));
    enableAttachment = std::make_unique<ButtonAttachment> (effect.getEnabledParameter(), enableButton);
    addAndMakeVisible (enableButton);

    for (const auto& parameter : effect.getParameters())
    {
        Control control;

        switch (parameter->getKind())
        {
            case Parameter::Kind::choice:
            {
                auto combo = std::make_unique<juce::ComboBox>();
                control.attachment = std::make_unique<ComboBoxAttachment> (*parameter, *combo);
                control.editor = std::move (combo);
                break;
            }

            case Parameter::Kind::toggle:
            {
                auto button = std::make_unique<juce::ToggleButton> (tr (parameter->getName()));
                control.attachment = std::make_unique<ButtonAttachment> (*parameter, *button);
                control.editor = std::move (button);
                break;
            }

            case Parameter::Kind::continuous:
            {
                auto slider = std::make_unique<juce::Slider> (juce::Slider::RotaryHorizontalVerticalDrag,
                                                              juce::Slider::TextBoxBelow);
                slider->setTextBoxStyle (juce::Slider::TextBoxBelow, false, columnWidth, 16);
                control.attachment = std::make_unique<SliderAttachment> (*parameter, *slider);
                control.editor = std::move (slider);
                break;
            }
        }

        // Los nombres de parámetros y efectos están en español: se traducen al mostrarlos.
        control.label = std::make_unique<juce::Label> (juce::String(), tr (parameter->getName()));
        control.label->setFont (juce::FontOptions (11.0f));
        control.label->setColour (juce::Label::textColourId, Palette::textDim);
        control.label->setJustificationType (juce::Justification::centred);

        addAndMakeVisible (*control.label);
        addAndMakeVisible (*control.editor);
        controls.push_back (std::move (control));
    }

    effect.getEnabledParameter().addListener (this);
    updateEnablement();
    setSize (getPreferredWidth(), headerHeight + 2 * rowHeight + padding);
}

EffectPanel::~EffectPanel()
{
    effect.getEnabledParameter().removeListener (this);
}

int EffectPanel::getNumColumns() const
{
    return juce::jmax (1, (static_cast<int> (controls.size()) + 1) / 2);
}

int EffectPanel::getPreferredWidth() const
{
    return juce::jmax (110, getNumColumns() * columnWidth + 2 * padding);
}

void EffectPanel::parameterChanged (Parameter&)
{
    updateEnablement();
}

void EffectPanel::updateEnablement()
{
    const auto alpha = effect.isEnabled() ? 1.0f : 0.45f;

    for (auto& control : controls)
    {
        control.editor->setAlpha (alpha);
        control.label->setAlpha (alpha);
    }

    repaint();
}

void EffectPanel::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (2.0f);

    g.setColour (Palette::panel);
    g.fillRoundedRectangle (bounds, 6.0f);

    g.setColour (effect.isEnabled() ? Palette::accent.withAlpha (0.6f) : Palette::outline);
    g.drawRoundedRectangle (bounds, 6.0f, 1.0f);

    g.setColour (effect.isEnabled() ? Palette::text : Palette::textDim);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (tr (effect.getName()), getLocalBounds().removeFromTop (headerHeight).reduced (padding + 2, 0),
                juce::Justification::centredLeft, true);
}

void EffectPanel::resized()
{
    auto bounds = getLocalBounds().reduced (padding, 0);
    auto header = bounds.removeFromTop (headerHeight);
    enableButton.setBounds (header.removeFromRight (26).withSizeKeepingCentre (24, 24));

    const auto columns = getNumColumns();

    for (size_t i = 0; i < controls.size(); ++i)
    {
        const auto column = static_cast<int> (i) % columns;
        const auto row = static_cast<int> (i) / columns;
        const auto cell = juce::Rectangle<int> (bounds.getX() + column * columnWidth, bounds.getY() + row * rowHeight,
                                                columnWidth, rowHeight).reduced (2, 0);

        auto area = cell;
        controls[i].label->setBounds (area.removeFromTop (labelHeight));

        if (dynamic_cast<juce::ComboBox*> (controls[i].editor.get()) != nullptr)
            controls[i].editor->setBounds (area.withSizeKeepingCentre (area.getWidth(), 24));
        else
            controls[i].editor->setBounds (area);
    }
}
}
