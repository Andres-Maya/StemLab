#include "TrackView.h"

namespace stemlab
{
TrackView::TrackView (std::shared_ptr<AudioTrack> audioTrack, juce::Colour trackColour,
                      juce::AudioFormatManager& formatManager, juce::AudioThumbnailCache& cache)
    : track (std::move (audioTrack)),
      colour (trackColour),
      deleteButton ("Eliminar pista", IconButton::Icon::close),
      meter ([t = track.get()] (int channel) { return t->getAndResetPeak (channel); }),
      waveform (*track, formatManager, cache),
      volumeAttachment (track->getVolume(), volumeSlider),
      panAttachment (track->getPan(), panSlider),
      muteAttachment (track->getMute(), muteButton),
      soloAttachment (track->getSolo(), soloButton)
{
    nameLabel.setText (track->getName(), juce::dontSendNotification);
    nameLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    nameLabel.setEditable (false, true);
    nameLabel.setTooltip ("Doble clic para renombrar");
    nameLabel.onTextChange = [this] { track->setName (nameLabel.getText()); };
    addAndMakeVisible (nameLabel);

    muteButton.setColour (juce::TextButton::buttonOnColourId, Palette::mute);
    soloButton.setColour (juce::TextButton::buttonOnColourId, Palette::solo);
    addAndMakeVisible (muteButton);
    addAndMakeVisible (soloButton);

    deleteButton.onClick = [this] { if (onDelete != nullptr) onDelete (*this); };
    addAndMakeVisible (deleteButton);

    volumeSlider.setPopupDisplayEnabled (true, true, nullptr);
    volumeSlider.setColour (juce::Slider::trackColourId, colour);
    addAndMakeVisible (volumeSlider);

    panSlider.setPopupDisplayEnabled (true, true, nullptr);
    addAndMakeVisible (panSlider);

    addAndMakeVisible (meter);

    waveform.setWaveColour (colour);
    waveform.setDimmed (track->getMute().getBool());
    waveform.onSeek = [this] (double seconds)
    {
        if (onSelect != nullptr)
            onSelect (*this);

        if (onSeek != nullptr)
            onSeek (seconds);
    };
    addAndMakeVisible (waveform);

    track->getMute().addListener (this);
}

TrackView::~TrackView()
{
    track->getMute().removeListener (this);
}

void TrackView::setSelected (bool shouldBeSelected)
{
    if (selected != shouldBeSelected)
    {
        selected = shouldBeSelected;
        repaint();
    }
}

void TrackView::setTimelineLength (double seconds)
{
    waveform.setTimelineLength (seconds);
}

void TrackView::parameterChanged (Parameter&)
{
    waveform.setDimmed (track->getMute().getBool());
}

void TrackView::paint (juce::Graphics& g)
{
    auto header = getLocalBounds().removeFromLeft (headerWidth).toFloat();

    g.setColour (selected ? Palette::panelLight : Palette::panel);
    g.fillRect (header);

    // Franja de color de la pista.
    g.setColour (colour);
    g.fillRect (header.removeFromLeft (4.0f));

    g.setColour (Palette::outline);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, static_cast<float> (getWidth()));
    g.drawVerticalLine (headerWidth - 1, 0.0f, static_cast<float> (getHeight()));
}

void TrackView::resized()
{
    auto header = getLocalBounds().removeFromLeft (headerWidth).reduced (10, 6).withTrimmedLeft (4);
    waveform.setBounds (getLocalBounds().withTrimmedLeft (headerWidth).withTrimmedBottom (1));

    meter.setBounds (header.removeFromRight (8));
    header.removeFromRight (6);

    auto top = header.removeFromTop (24);
    deleteButton.setBounds (top.removeFromRight (22).reduced (1));
    top.removeFromRight (4);
    soloButton.setBounds (top.removeFromRight (26));
    top.removeFromRight (4);
    muteButton.setBounds (top.removeFromRight (26));
    top.removeFromRight (4);
    nameLabel.setBounds (top);

    header.removeFromTop (6);
    auto bottom = header.removeFromTop (38);
    panSlider.setBounds (bottom.removeFromRight (38));
    bottom.removeFromRight (6);
    volumeSlider.setBounds (bottom.withSizeKeepingCentre (bottom.getWidth(), 22));
}

void TrackView::mouseDown (const juce::MouseEvent&)
{
    if (onSelect != nullptr)
        onSelect (*this);
}
}
