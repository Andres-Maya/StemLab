#include "FileAssociation.h"

#include "Project/Project.h"

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
std::vector<std::pair<juce::String, juce::String>> FileAssociation::registryValuesFor (const juce::File& executable)
{
    const juce::String classes ("HKEY_CURRENT_USER\\Software\\Classes\\");
    const juce::String type (progId);
    const auto quotedExe = executable.getFullPathName().quoted();

    return {
        { classes + Project::fileExtension + "\\",              type },
        { classes + type + "\\",                                "Proyecto de StemLab" },
        // Icono 0 del ejecutable: el de la aplicación (Resources/StemLab.png).
        { classes + type + "\\DefaultIcon\\",                   quotedExe + ",0" },
        // Entre comillas: las rutas con espacios también funcionan.
        { classes + type + "\\shell\\open\\command\\",          quotedExe + " \"%1\"" },
    };
}

bool FileAssociation::registerProjectFiles (const juce::File& executable)
{
   #if JUCE_WINDOWS
    auto changed = false;

    for (const auto& [path, value] : registryValuesFor (executable))
    {
        if (juce::WindowsRegistry::getValue (path) != value)
        {
            juce::WindowsRegistry::setValue (path, value);
            changed = true;
        }
    }

    // Sin este aviso el Explorador sigue mostrando el icono genérico hasta reiniciar.
    if (changed)
        SHChangeNotify (SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

    return changed;
   #else
    juce::ignoreUnused (executable);
    return false;
   #endif
}
}
