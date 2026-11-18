#!/usr/bin/env python
import os
import sys
# from scons_compiledb import compile_db # Import the compile_db function # Call the compile_db function to enable compile_commands.json generation 
# compile_db()


# Target the Godot 4.7 API by default (godot-cpp 10 ships the 4.7 extension_api.json).
# Override on the command line with e.g. `scons api_version=4.7`.
ARGUMENTS.setdefault("api_version", "4.7")

# Import the SConstruct from godot-cpp
env = SConscript("godot-cpp/SConstruct")

# Add necessary include directories
env.Append(CPPPATH=["src/"])

# Add main source files
sources = Glob("src/*.cpp") + Glob("src/bvh/*.cpp") + Glob("src/rd/*.cpp") + Glob("src/path_tracing/*.cpp")

#compiler flags
if env['PLATFORM'] == 'windows':
    if env['CXX'] == 'x86_64-w64-mingw32-g++':
        pass
    elif env['CXX'] == 'cl':
        env.Append(CXXFLAGS=['/EHsc'])  # Apply /EHsc for MSVC


# Handle different platforms
if env["platform"] == "macos":
    library = env.SharedLibrary(
        "demo/addons/jar_path_tracing/bin/jar_path_tracing.{}.{}.framework/jar_path_tracing.{}.{}".format(
            env["platform"], env["target"], env["platform"], env["target"]
        ),
        source=sources,
    )
elif env["platform"] == "ios":
    if env["ios_simulator"]:
        library = env.StaticLibrary(
            "demo/addons/jar_path_tracing/bin/jar_path_tracing.{}.{}.simulator.a".format(env["platform"], env["target"]),
            source=sources,
        )
    else:
        library = env.StaticLibrary(
            "demo/addons/jar_path_tracing/bin/jar_path_tracing.{}.{}.a".format(env["platform"], env["target"]),
            source=sources,
        )
else:
    library = env.SharedLibrary(
        "demo/addons/jar_path_tracing/bin/jar_path_tracing{}{}".format(env["suffix"], env["SHLIBSUFFIX"]),
        source=sources,
    )

Default(library)
