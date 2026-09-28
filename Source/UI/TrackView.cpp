#include "TrackView.h"

#include "Utils/Strings.h"

namespace stemlab
{
namespace
{
    constexpr int reorderThreshold = 5;     // píxeles antes de empezar a mover la pista
}

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
    nameLabel.setTooltip ("Doble clic (o F2) para cambiar el nombre. Arrastra para mover la pista.");
    nameLabel.onTextChange = [this]
    {
        const auto newName = nameLabel.getText().trim();

        if (newName.isEmpty())
        {
            nameLabel.setText (track->getName(), juce::dontSendNotification);
            return;
        }

        const auto oldName = track->getName();

        if (newName == oldName)
            return;

        track->setName (newName);

        if (onRenamed != nullptr)
            onRenamed (*this, oldName);
    };

    // La fila recibe también los clics sobre el nombre: así se puede arrastrar
    // la pista agarrándola por el nombre (el doble clic sigue editándolo).
    nameLabel.addMouseListener (this, false);
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
    waveform.onClipsEdited = [this] (std::vector<AudioClip> clipsBefore, const juce::String& actionName)
    {
        if (onClipsEdited != nullptr)
            onClipsEdited (*this, std::move (clipsBefore), actionName);
    };
    waveform.onWheel = [this] (int x, const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
    {
        return onWheel != nullptr && onWheel (x, e, w);
    };
    addAndMakeVisible (waveform);

    track->getMute().addListener (this);
}

TrackView::~TrackView()
{
    nameLabel.removeMouseListener (this);
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

void TrackView::setFolderColour (juce::Colour newColour)
{
    if (folderColour != newColour)
    {
        folderColour = newColour;
        repaint();
    }
}

void TrackView::trackChanged()
{
    if (! nameLabel.isBeingEdited())
        nameLabel.setText (track->getName(), juce::dontSendNotification);

    waveform.clipsChanged();
    repaint();      // la franja se pone roja mientras se graba en esta pista
}

void TrackView::startRename()
{
    nameLabel.showEditor();
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

    // Dentro de una carpeta: una franja de su color, como sangría.
    if (! folderColour.isTransparent())
    {
        g.setColour (folderColour.withAlpha (0.55f));
        g.fillRect (header.removeFromLeft (6.0f));
        header.removeFromLeft (2.0f);
    }

    // Franja de color de la pista (roja mientras se graba en ella).
    g.setColour (track->isArmed() ? Palette::record : colour);
    g.fillRect (header.removeFromLeft (4.0f));

    g.setColour (Palette::outline);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, static_cast<float> (getWidth()));
    g.drawVerticalLine (headerWidth - 1, 0.0f, static_cast<float> (getHeight()));

    // Mientras se arrastra, un borde del color de la pista la resalta.
    if (reordering)
    {
        g.setColour (colour);
        g.drawRect (getLocalBounds(), 2);
    }
}

void TrackView::resized()
{
    auto header = getLocalBounds().removeFromLeft (headerWidth).reduced (10, 6).withTrimmedLeft (4);
    waveform.setBounds (getLocalBounds().withTrimmedLeft (headerWidth).withTrimmedBottom (1));

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

//==============================================================================
void TrackView::mouseDown (const juce::MouseEvent& event)
{
    if (onHeaderClicked != nullptr)
        onHeaderClicked (*this);

    if (event.mods.isPopupMenu())
    {
        if (onHeaderMenu != nullptr)
            onHeaderMenu (*this);

        return;
    }

    grabY = event.getEventRelativeTo (this).y;
    reordering = false;
}

void TrackView::mouseDrag (const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu() || nameLabel.isBeingEdited() || getParentComponent() == nullptr)
        return;

    if (! reordering && std::abs (event.getDistanceFromDragStartY()) < reorderThreshold)
        return;

    if (! reordering)
    {
        reordering = true;
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        repaint();
    }

    if (onReorderDrag != nullptr)
        onReorderDrag (*this, event.getEventRelativeTo (getParentComponent()).y, grabY);
}

void TrackView::mouseUp (const juce::MouseEvent&)
{
    if (! reordering)
        return;

    reordering = false;
    setMouseCursor (juce::MouseCursor::NormalCursor);
    repaint();

    if (onReorderEnd != nullptr)
        onReorderEnd (*this);
}
}
