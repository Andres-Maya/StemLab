#include "TrackView.h"

#include "Utils/Strings.h"

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
    waveform.onClipClicked = [this] (juce::uint32 clipId) { if (onClipClicked != nullptr) onClipClicked (*this, clipId); };
    waveform.onContextMenu = [this] (juce::uint32 clipId, double seconds) { if (onContextMenu != nullptr) onContextMenu (*this, clipId, seconds); };
    waveform.onClipsEdited = [this] { if (onClipsEdited != nullptr) onClipsEdited(); };
    waveform.onWheel = [this] (int x, const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
    {
        return onWheel != nullptr && onWheel (x, e, w);
    };
    addAndMakeVisible (waveform);

    addBelowButton.setTooltip ("Añadir una pista debajo"_u8);
    addBelowButton.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    addBelowButton.onClick = [this] { if (onAddBelow != nullptr) onAddBelow (*this); };
    addBelowButton.colour = colour;
    addChildComponent (addBelowButton);

    addMouseListener (&hoverWatcher, true);
    track->getMute().addListener (this);
}

TrackView::~TrackView()
{
    removeMouseListener (&hoverWatcher);
    track->getMute().removeListener (this);
}

void TrackView::setIsLast (bool shouldBeLast)
{
    isLast = shouldBeLast;
    updateAddButton();
}

void TrackView::updateAddButton()
{
    addBelowButton.setVisible (isLast || isMouseOver (true));
}

//==============================================================================
juce::Point<float> TrackView::AddBelowButton::getCircleCentre() const
{
    // Pegado al borde inferior: el círculo se apoya en la línea del borde.
    return { 6.0f + radius, static_cast<float> (getHeight()) - 1.0f - radius };
}

bool TrackView::AddBelowButton::hitTest (int x, int y)
{
    // Solo el círculo responde al ratón; el resto de la franja sigue siendo
    // la cabecera de la pista (clic para seleccionar).
    return getCircleCentre().getDistanceFrom ({ static_cast<float> (x), static_cast<float> (y) }) <= radius + 3.0f;
}

void TrackView::AddBelowButton::paint (juce::Graphics& g)
{
    const auto centre = getCircleCentre();
    const auto lineY = static_cast<float> (getHeight()) - 2.0f;

    // Del color de la pista, algo apagado; al pasar el ratón, a pleno color.
    const auto lineColour = colour.withAlpha (hovered ? 1.0f : 0.65f);

    // Línea sobre el borde inferior, solo en la cabecera (hasta donde empiezan
    // los clips), saliendo del propio círculo.
    g.setColour (lineColour);
    g.fillRect (centre.x, lineY, static_cast<float> (getWidth()) - centre.x, 2.0f);

    g.setColour (Palette::panel.interpolatedWith (colour, hovered ? 0.35f : 0.15f));
    g.fillEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
    g.setColour (lineColour);
    g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.5f);

    g.setColour (hovered ? Palette::text : Palette::text.withAlpha (0.8f));
    g.fillRoundedRectangle (centre.x - 4.0f, centre.y - 0.75f, 8.0f, 1.5f, 0.75f);
    g.fillRoundedRectangle (centre.x - 0.75f, centre.y - 4.0f, 1.5f, 8.0f, 0.75f);
}

void TrackView::AddBelowButton::mouseUp (const juce::MouseEvent& event)
{
    if (hitTest (event.x, event.y) && onClick != nullptr)
        onClick();
}

void TrackView::setSelected (bool shouldBeSelected)
{
    if (selected != shouldBeSelected)
    {
        selected = shouldBeSelected;
        repaint();
    }
}

void TrackView::trackChanged()
{
    waveform.clipsChanged();
    repaint();      // la franja se pone roja mientras se graba en esta pista
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

    // Franja de color de la pista (roja mientras se graba en ella).
    g.setColour (track->isArmed() ? Palette::record : colour);
    g.fillRect (header.removeFromLeft (4.0f));

    g.setColour (Palette::outline);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, static_cast<float> (getWidth()));
    g.drawVerticalLine (headerWidth - 1, 0.0f, static_cast<float> (getHeight()));
}

void TrackView::resized()
{
    auto header = getLocalBounds().removeFromLeft (headerWidth).reduced (10, 6).withTrimmedLeft (4);
    waveform.setBounds (getLocalBounds().withTrimmedLeft (headerWidth).withTrimmedBottom (1));
    addBelowButton.setBounds (0, getHeight() - AddBelowButton::height, headerWidth - 1, AddBelowButton::height);

    meter.setBounds (header.removeFromRight (8));
    header.removeFromRight (6);

    auto top = header.removeFromTop (24);
    deleteButton.setBounds (top.removeFromRight (22).reduced (1));
    top.removeFromRight (3);
    soloButton.setBounds (top.removeFromRight (24));
    top.removeFromRight (3);
    muteButton.setBounds (top.removeFromRight (24));
    top.removeFromRight (3);
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
