#include "Project.h"

namespace stemlab
{
Project::Project (juce::String projectName, juce::File projectDirectory, bool isTemporary)
    : name (std::move (projectName)),
      directory (std::move (projectDirectory)),
      temporary (isTemporary)
{
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
