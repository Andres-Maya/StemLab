#include "FileAssociation.h"

#include "Project/Project.h"

#include <StemLabBinaryData.h>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <shlobj.h>
#endif

namespace stemlab
{
std::vector<std::pair<juce::String, juce::String>> FileAssociation::registryValuesFor (const juce::File& executable,
                                                                                       const juce::File& iconFile)
{
    const juce::String classes ("HKEY_CURRENT_USER\\Software\\Classes\\");
    const juce::String type (progId);

    return {
        { classes + Project::fileExtension + "\\",              type },
        { classes + type + "\\",                                "Proyecto de StemLab" },
        { classes + type + "\\DefaultIcon\\",                   iconFile.getFullPathName().quoted() },
        // Entre comillas: las rutas con espacios también funcionan.
        { classes + type + "\\shell\\open\\command\\",          executable.getFullPathName().quoted() + " \"%1\"" },
    };
}

juce::File FileAssociation::writeProjectIcon (const juce::File& folder)
{
    const auto* data = StemLabBinaryData::StemLab_ico;
    const auto size = static_cast<size_t> (StemLabBinaryData::StemLab_icoSize);

    // Huella del contenido (FNV-1a de 64 bits): cambia si cambia el icono.
    juce::uint64 hash = 14695981039346656037ull;

    for (size_t i = 0; i < size; ++i)
        hash = (hash ^ static_cast<juce::uint8> (data[i])) * 1099511628211ull;

    const auto icon = folder.getChildFile ("StemLab-" + juce::String::toHexString (static_cast<juce::int64> (hash)) + ".ico");

    if (folder.createDirectory().failed())
        return {};

    if (! icon.existsAsFile() || static_cast<size_t> (icon.getSize()) != size)
        if (! icon.replaceWithData (data, size))
            return {};

    // Los iconos de versiones anteriores ya no los usa nadie.
    for (const auto& old : folder.findChildFiles (juce::File::findFiles, false, "StemLab-*.ico"))
        if (old != icon)
            old.deleteFile();

    return icon;
}

bool FileAssociation::registerProjectFiles (const juce::File& executable)
{
   #if JUCE_WINDOWS
    const auto iconFolder = juce::File::getSpecialLocation (juce::File::windowsLocalAppData)
                                .getChildFile ("StemLab").getChildFile ("Icons");
    const auto icon = writeProjectIcon (iconFolder);

    if (icon == juce::File())
        return false;

    auto changed = false;

    for (const auto& [path, value] : registryValuesFor (executable, icon))
    {
        if (juce::WindowsRegistry::getValue (path) != value)
        {
            juce::WindowsRegistry::setValue (path, value);
            changed = true;
        }
    }

    // Sin este aviso el Explorador sigue mostrando el icono anterior hasta reiniciar.
    if (changed)
        SHChangeNotify (SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

    return changed;
   #else
    juce::ignoreUnused (executable);
    return false;
   #endif
}
}
