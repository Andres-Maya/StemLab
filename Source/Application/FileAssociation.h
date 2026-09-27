#pragma once

#include <juce_core/juce_core.h>

#include <utility>
#include <vector>

namespace stemlab
{
/**
    Asociación de los archivos .stemlab con StemLab en Windows: el Explorador
    les pone el icono de StemLab y los abre con doble clic.

    Se registra solo para el usuario actual (HKEY_CURRENT_USER\Software\Classes,
    sin permisos de administrador). Para quitarla basta con borrar las claves
    ".stemlab" y "StemLab.Project" de esa rama del registro.

    El icono de los proyectos es un .ico propio (Resources/StemLab.ico, dentro
    del ejecutable) que se escribe en AppData\Local\StemLab\Icons con un nombre
    que depende de su contenido: si el icono cambia, cambia la ruta y Windows
    lo vuelve a leer en vez de seguir mostrando el que tenía en caché.
*/
namespace FileAssociation
{
    /** Identificador del tipo de archivo en el registro. */
    constexpr auto progId = "StemLab.Project";

    /** Valores de registro (ruta completa, valor) que asocian los .stemlab con
        ese ejecutable y ese icono. Las rutas terminadas en '\' son el valor por
        defecto de la clave. No escribe nada: sirve para registrar y para las pruebas. */
    std::vector<std::pair<juce::String, juce::String>> registryValuesFor (const juce::File& executable,
                                                                          const juce::File& iconFile);

    /** Escribe el icono de los proyectos en la carpeta (StemLab-<huella>.ico)
        si no estaba, borra los de versiones anteriores y devuelve su ruta. */
    juce::File writeProjectIcon (const juce::File& folder);

    /** Escribe el icono y los valores de registro que falten o hayan cambiado
        (la primera vez, si StemLab.exe cambió de sitio o si cambió el icono) y
        avisa al Explorador para que refresque los iconos. Devuelve true si tuvo
        que escribir algo en el registro. En otros sistemas no hace nada. Lo
        llama la aplicación al arrancar (nunca las pruebas). */
    bool registerProjectFiles (const juce::File& executable);
}
}
