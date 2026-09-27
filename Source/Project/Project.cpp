#include "Project.h"

namespace stemlab
{
Project::Project (juce::String projectName, juce::File projectDirectory, bool isTemporary)
    : name (std::move (projectName)),
      directory (std::move (projectDirectory)),
      projectFile (projectFileFor (directory)),
      temporary (isTemporary)
{
}

juce::File Project::projectFileFor (const juce::File& folder)
{
    return folder.getChildFile (folder.getFileName() + fileExtension);
}

juce::File Project::findProjectFileIn (const juce::File& folder)
{
    if (const auto preferred = projectFileFor (folder); preferred.existsAsFile())
        return preferred;

    auto others = folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + fileExtension);
    others.sort();

    if (! others.isEmpty())
        return others.getFirst();

    if (const auto legacy = folder.getChildFile ("project.json"); legacy.existsAsFile())
        return legacy;

    return {};
}

bool Project::isProjectFile (const juce::File& file)
{
    return file.hasFileExtension (fileExtension) || file.getFileName() == "project.json";
}

juce::Result Project::createFolderStructure() const
{
    if (const auto result = directory.createDirectory(); result.failed())
        return result;

    for (const auto& subfolder : getSubfolderNames())
        if (const auto result = directory.getChildFile (subfolder).createDirectory(); result.failed())
            return result;

    return juce::Result::ok();
}

juce::String Project::toStoredPath (const juce::File& file) const
{
    if (file.isAChildOf (directory))
        return file.getRelativePathFrom (directory).replaceCharacter ('\\', '/');

    return file.getFullPathName();
}

juce::File Project::fromStoredPath (const juce::String& storedPath) const
{
    if (juce::File::isAbsolutePath (storedPath))
        return juce::File (storedPath);

    return directory.getChildFile (storedPath);
}
}
