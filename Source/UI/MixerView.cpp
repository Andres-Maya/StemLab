#include "MixerView.h"

#include "LevelMeter.h"
#include "StemLabLookAndFeel.h"
#include "Utils/Strings.h"

namespace stemlab
{
namespace
{
    constexpr int titleHeight = 30;
    constexpr int panelGap = 6;
}

/** Tira de canal: volumen vertical, paneo y medidor de la pista. */
class MixerView::ChannelStrip final : public juce::Component
{
public:
    explicit ChannelStrip (AudioTrack& t)
        : meter ([&t] (int channel) { return t.getAndResetPeak (channel); }),
          volumeAttachment (t.getVolume(), volume),
          panAttachment (t.getPan(), pan)
    {
        volume.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
        pan.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 16);

        for (auto* label : { &volumeLabel, &panLabel })
        {
            label->setFont (juce::FontOptions (11.0f));
            label->setColour (juce::Label::textColourId, Palette::textDim);
            label->setJustificationType (juce::Justification::centred);
            addAndMakeVisible (*label);
        }

        addAndMakeVisible (volume);
        addAndMakeVisible (pan);
        addAndMakeVisible (meter);
        setSize (150, 200);
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (bounds, 6.0f);
        g.setColour (Palette::outline);
        g.drawRoundedRectangle (bounds, 6.0f, 1.0f);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced (8);
        meter.setBounds (bounds.removeFromRight (10));
        bounds.removeFromRight (4);

        auto left = bounds.removeFromLeft (bounds.getWidth() / 2);
        volumeLabel.setBounds (left.removeFromTop (14));
        volume.setBounds (left);

        panLabel.setBounds (bounds.removeFromTop (14));
        pan.setBounds (bounds.removeFromTop (78));
    }

private:
    juce::Label volumeLabel { {}, "Volumen" };
    juce::Label panLabel { {}, "Paneo" };
    juce::Slider volume { juce::Slider::LinearVertical, juce::Slider::TextBoxBelow };
    juce::Slider pan { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    LevelMeter meter;

    SliderAttachment volumeAttachment;
    SliderAttachment panAttachment;
};

//==============================================================================
MixerView::MixerView()
{
    viewport.setViewedComponent (&rack, false);
    viewport.setScrollBarsShown (false, true);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);
}

MixerView::~MixerView() = default;

void MixerView::setTrack (std::shared_ptr<AudioTrack> newTrack)
{
    if (newTrack == track)
        return;

    // Primero los paneles (sus attachments apuntan a la pista anterior).
    panels.clear();
    strip.reset();
    track = std::move (newTrack);

    if (track != nullptr)
    {
        strip = std::make_unique<ChannelStrip> (*track);
        rack.addAndMakeVisible (*strip);

        for (const auto& effect : track->getEffects().getEffects())
        {
            auto panel = std::make_unique<EffectPanel> (*effect);
            rack.addAndMakeVisible (*panel);
            panels.push_back (std::move (panel));
        }
    }

    resized();
    repaint();
}

void MixerView::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background);

    auto title = getLocalBounds().removeFromTop (titleHeight).reduced (12, 0);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.setColour (Palette::textDim);
    g.drawText ("MEZCLADOR", title, juce::Justification::centredLeft, false);

    if (track != nullptr)
    {
        g.setColour (Palette::text);
        g.drawText (track->getName(), title.withTrimmedLeft (100), juce::Justification::centredLeft, true);
    }
    else
    {
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("Selecciona una pista para ver su canal y sus efectos.",
                    getLocalBounds().withTrimmedTop (titleHeight), juce::Justification::centred, true);
    }

    g.setColour (Palette::outline);
    g.drawHorizontalLine (0, 0.0f, static_cast<float> (getWidth()));
}

void MixerView::resized()
{
    viewport.setBounds (getLocalBounds().withTrimmedTop (titleHeight).reduced (8, 0).withTrimmedBottom (4));

    const auto height = viewport.getHeight() - viewport.getScrollBarThickness();
    int x = 0;

    if (strip != nullptr)
    {
        strip->setBounds (x, 0, strip->getWidth(), height);
        x += strip->getWidth() + panelGap;
    }

    for (auto& panel : panels)
    {
        panel->setBounds (x, 0, panel->getPreferredWidth(), height);
        x += panel->getWidth() + panelGap;
    }

    rack.setSize (juce::jmax (x, viewport.getWidth()), height);
}
}
