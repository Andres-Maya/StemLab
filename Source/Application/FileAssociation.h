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
*/
namespace FileAssociation
{
    /** Identificador del tipo de archivo en el registro. */
    constexpr auto progId = "StemLab.Project";

    /** Valores de registro (ruta completa, valor) que asocian los .stemlab con
        ese ejecutable. Las rutas terminadas en '\' son el valor por defecto de
        la clave. No escribe nada: sirve para registrar y para las pruebas. */
    std::vector<std::pair<juce::String, juce::String>> registryValuesFor (const juce::File& executable);

    /** Escribe los valores que falten o hayan cambiado (la primera vez, o si
        StemLab.exe cambió de sitio) y avisa al Explorador para que refresque
        los iconos. Devuelve true si tuvo que escribir algo. En otros sistemas
        no hace nada. Lo llama la aplicación al arrancar (nunca las pruebas). */
    bool registerProjectFiles (const juce::File& executable);
}
}
