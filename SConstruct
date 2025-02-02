#!/usr/bin/env python
import os
import sys
# from scons_compiledb import compile_db # Import the compile_db function # Call the compile_db function to enable compile_commands.json generation 
# compile_db()


# Import the SConstruct from godot-cpp
env = SConscript("godot-cpp/SConstruct")

# Add necessary include directories
env.Append(CPPPATH=[
    "src/",
    "src/path_tracing/",
    "src/path_tracing/post_processing",
    "src/jarcs/include/",
])

# Add main source files
sources = Glob("src/*.cpp") + Glob("src/path_tracing/*.cpp") + Glob("src/jarcs/src/*.cpp")  + Glob("src/bvh/*.cpp") + Glob("src/path_tracing/post_processing/*.cpp")

#compiler flags
if env['PLATFORM'] == 'windows':
    if env['CXX'] == 'x86_64-w64-mingw32-g++':
        env.Append(CXXFLAGS=['-std=c++11'])  # Example flags for MinGW
    elif env['CXX'] == 'cl':
        env.Append(CXXFLAGS=['/EHsc'])  # Apply /EHsc for MSVC


# Handle different platforms
if env["platform"] == "macos":
    library = env.SharedLibrary(
        "project/addons/jar_path_tracing/bin/jar_path_tracing.{}.{}.framework/jar_path_tracing.{}.{}".format(
            env["platform"], env["target"], env["platform"], env["target"]
        ),
        source=sources,
    )
elif env["platform"] == "ios":
    if env["ios_simulator"]:
        library = env.StaticLibrary(
            "project/addons/jar_path_tracing/bin/jar_path_tracing.{}.{}.simulator.a".format(env["platform"], env["target"]),
            source=sources,
        )
    else:
        library = env.StaticLibrary(
            "project/addons/jar_path_tracing/bin/jar_path_tracing.{}.{}.a".format(env["platform"], env["target"]),
            source=sources,
        )
else:
    library = env.SharedLibrary(
        "project/addons/jar_path_tracing/bin/jar_path_tracing{}{}".format(env["suffix"], env["SHLIBSUFFIX"]),
        source=sources,
    )

Default(library)
