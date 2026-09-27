# Pruebas automáticas de StemLab: un ejecutable de consola que compila el mismo
# código que la aplicación (STEMLAB_SOURCES) más las pruebas. Ver README.md, "Pruebas".

juce_add_console_app(StemLabTests PRODUCT_NAME "StemLabTests")

target_sources(StemLabTests PRIVATE
    TestMain.cpp
    TestSupport.cpp
    UnitTests.cpp
    EditingTests.cpp
    ExportTests.cpp
    UiTests.cpp
    DeviceTests.cpp
    PythonTests.cpp
    ${STEMLAB_SOURCES})

stemlab_configure_target(StemLabTests)

target_include_directories(StemLabTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_compile_definitions(StemLabTests PRIVATE
    # runDispatchLoopUntil(): las pruebas esperan cargas asíncronas y el audio real.
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_APPLICATION_NAME_STRING="StemLabTests"
    JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")

# ctest ejecuta las pruebas rápidas, que no necesitan tarjeta de sonido ni
# Python. Las demás se piden al ejecutable: --device, --python, --all.
add_test(NAME StemLab.unit COMMAND StemLabTests)
