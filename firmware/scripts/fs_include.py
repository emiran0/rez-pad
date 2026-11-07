# PlatformIO pre-build script.
# Arduino's USB library includes FS.h from its mass-storage files, but PlatformIO does not give the
# library FS's include path (its library.properties `depends=FS` is not honoured for framework
# libraries). Add it globally so every library build can see it.
import os

Import("env")  # noqa: F821 (provided by PlatformIO)

framework = env.PioPlatform().get_package_dir("framework-arduinoespressif32")  # noqa: F821
fs_src = os.path.join(framework, "libraries", "FS", "src")
env.Append(CPPPATH=[fs_src])  # noqa: F821
DefaultEnvironment().Append(CPPPATH=[fs_src])  # noqa: F821
