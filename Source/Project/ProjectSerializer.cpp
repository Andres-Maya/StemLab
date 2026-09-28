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
    if (! project.getProjectFile().replaceWithText (toJson (project, document)))
        return juce::Result::fail ("No se pudo escribir " + project.getProjectFile().getFullPathName());

    return juce::Result::ok();
}

juce::String ProjectSerializer::toJson (const Project& project, const ProjectDocument& document)
{
    juce::Array<juce::var> tracks;

    for (const auto& track : document.tracks)
    {
        juce::Array<juce::var> clips;

        for (const auto& clip : track.clips)
        {
            auto* clipObject = new juce::DynamicObject();
            clipObject->setProperty ("file", project.toStoredPath (clip.file));
            clipObject->setProperty ("start", clip.startSeconds);
            clipObject->setProperty ("offset", clip.offsetSeconds);
            clipObject->setProperty ("length", clip.lengthSeconds);
            clips.add (juce::var (clipObject));
        }

        auto* object = new juce::DynamicObject();
        object->setProperty ("name", track.name);
        object->setProperty ("state", track.state);
        object->setProperty ("clips", clips);
        tracks.add (juce::var (object));
    }

    auto* root = new juce::DynamicObject();
    root->setProperty ("format", formatName);
    root->setProperty ("version", currentVersion);
    root->setProperty ("name", project.getName());
    root->setProperty ("bpm", project.getBpm());
    root->setProperty ("masterVolume", document.masterVolumeDb);
    root->setProperty ("tracks", tracks);

    if (! document.folders.empty())
    {
        juce::Array<juce::var> folders;

        for (const auto& folder : document.folders)
        {
            auto* object = new juce::DynamicObject();
            object->setProperty ("id", folder.id);
            object->setProperty ("name", folder.name);
            object->setProperty ("colour", folder.colour.toDisplayString (true));
            object->setProperty ("expanded", folder.expanded);

            juce::Array<juce::var> stems;

            for (const auto& stem : folder.stems)
                stems.add (stem);

            object->setProperty ("stems", stems);

            if (folder.sourceFile != juce::File())
            {
                object->setProperty ("sourceFile", project.toStoredPath (folder.sourceFile));
                object->setProperty ("sourceStart", folder.sourceStartSeconds);
                object->setProperty ("sourceLength", folder.sourceLengthSeconds);
            }

            folders.add (juce::var (object));
        }

        root->setProperty ("folders", folders);
    }

    return juce::JSON::toString (juce::var (root), false);
}

juce::Result ProjectSerializer::read (const juce::File& projectFile, Project& project, ProjectDocument& document)
{
    if (! projectFile.existsAsFile())
        return juce::Result::fail ("No existe " + projectFile.getFullPathName());

    juce::var root;

    if (const auto parsed = juce::JSON::parse (projectFile.loadFileAsString(), root); parsed.failed())
        return juce::Result::fail (projectFile.getFileName() + " no es un JSON válido: "_u8 + parsed.getErrorMessage());

    if (root.getProperty ("format", {}).toString() != formatName)
        return juce::Result::fail ("El archivo no es un proyecto de StemLab.");

    const auto version = static_cast<int> (root.getProperty ("version", 0));

    if (version > currentVersion)
        return juce::Result::fail ("El proyecto se creó con una versión más reciente de StemLab."_u8);

    project = Project (root.getProperty ("name", projectFile.getParentDirectory().getFileName()).toString(),
                       projectFile.getParentDirectory(), false);
    project.setProjectFile (projectFile);
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
            description.state = track.getProperty ("state", {});

            if (version < 2)
            {
                // v1: un único archivo que empieza en "start" y dura entero.
                ClipDescription clip;
                clip.file = project.fromStoredPath (track.getProperty ("file", {}).toString());
                clip.startSeconds = track.getProperty ("start", 0.0);
                description.clips.push_back (clip);
            }
            else
            {
                const auto clipsVar = track.getProperty ("clips", {});

                if (const auto* clips = clipsVar.getArray())
                {
                    for (const auto& clipVar : *clips)
                    {
                        ClipDescription clip;
                        clip.file = project.fromStoredPath (clipVar.getProperty ("file", {}).toString());
                        clip.startSeconds = clipVar.getProperty ("start", 0.0);
                        clip.offsetSeconds = clipVar.getProperty ("offset", 0.0);
                        clip.lengthSeconds = clipVar.getProperty ("length", -1.0);
                        description.clips.push_back (clip);
                    }
                }
            }

            document.tracks.push_back (std::move (description));
        }
    }

    const auto foldersVar = root.getProperty ("folders", {});

    if (const auto* folders = foldersVar.getArray())
    {
        for (const auto& object : *folders)
        {
            TrackFolder folder;
            folder.id = object.getProperty ("id", {}).toString();
            folder.name = object.getProperty ("name", "Carpeta").toString();
            folder.colour = juce::Colour::fromString (object.getProperty ("colour", "ff4fc3f7").toString());
            folder.expanded = object.getProperty ("expanded", true);

            if (const auto* stems = object.getProperty ("stems", {}).getArray())
                for (const auto& stem : *stems)
                    folder.stems.add (stem.toString());

            if (const auto stored = object.getProperty ("sourceFile", {}).toString(); stored.isNotEmpty())
            {
                folder.sourceFile = project.fromStoredPath (stored);
                folder.sourceStartSeconds = object.getProperty ("sourceStart", 0.0);
                folder.sourceLengthSeconds = object.getProperty ("sourceLength", -1.0);
            }

            if (folder.id.isNotEmpty())
                document.folders.push_back (std::move (folder));
        }
    }

    return juce::Result::ok();
}
}
