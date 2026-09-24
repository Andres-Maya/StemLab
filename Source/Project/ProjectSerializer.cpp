#include "ProjectSerializer.h"

#include "Utils/Strings.h"

namespace stemlab
{
namespace
{
    constexpr auto formatName = "stemlab-project";
}

juce::Result ProjectSerializer::write (const Project& project, const ProjectDocument& document)
{
    juce::Array<juce::var> tracks;

    for (const auto& track : document.tracks)
    {
        auto* object = new juce::DynamicObject();
        object->setProperty ("name", track.name);
        object->setProperty ("file", project.toStoredPath (track.file));
        object->setProperty ("start", track.startSeconds);
        object->setProperty ("state", track.state);
        tracks.add (juce::var (object));
    }

    auto* root = new juce::DynamicObject();
    root->setProperty ("format", formatName);
    root->setProperty ("version", currentVersion);
    root->setProperty ("name", project.getName());
    root->setProperty ("bpm", project.getBpm());
    root->setProperty ("masterVolume", document.masterVolumeDb);
    root->setProperty ("tracks", tracks);

    const auto json = juce::JSON::toString (juce::var (root), false);

    if (! project.getProjectFile().replaceWithText (json))
        return juce::Result::fail ("No se pudo escribir " + project.getProjectFile().getFullPathName());

    return juce::Result::ok();
}

juce::Result ProjectSerializer::read (const juce::File& projectFile, Project& project, ProjectDocument& document)
{
    if (! projectFile.existsAsFile())
        return juce::Result::fail ("No existe " + projectFile.getFullPathName());

    juce::var root;

    if (const auto parsed = juce::JSON::parse (projectFile.loadFileAsString(), root); parsed.failed())
        return juce::Result::fail ("project.json no es un JSON válido: "_u8 + parsed.getErrorMessage());

    if (root.getProperty ("format", {}).toString() != formatName)
        return juce::Result::fail ("El archivo no es un proyecto de StemLab.");

    if (static_cast<int> (root.getProperty ("version", 0)) > currentVersion)
        return juce::Result::fail ("El proyecto se creó con una versión más reciente de StemLab."_u8);

    project = Project (root.getProperty ("name", projectFile.getParentDirectory().getFileName()).toString(),
                       projectFile.getParentDirectory(), false);
    project.setBpm (root.getProperty ("bpm", 120.0));

    document = {};
    document.masterVolumeDb = static_cast<float> (root.getProperty ("masterVolume", 0.0f));

    const auto tracksVar = root.getProperty ("tracks", {});

    if (const auto* tracks = tracksVar.getArray())
    {
        for (const auto& track : *tracks)
        {
            TrackDescription description;
            description.name = track.getProperty ("name", "Pista").toString();
            description.file = project.fromStoredPath (track.getProperty ("file", {}).toString());
            description.startSeconds = track.getProperty ("start", 0.0);
            description.state = track.getProperty ("state", {});
            document.tracks.push_back (std::move (description));
        }
    }

    return juce::Result::ok();
}
}
