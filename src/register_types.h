#ifndef JAR_PATH_TRACING_REGISTER_TYPES_H
#define JAR_PATH_TRACING_REGISTER_TYPES_H

#include <godot_cpp/core/class_db.hpp>
#include <gdextension_interface.h>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

void initialize_jar_path_tracing_module(ModuleInitializationLevel p_level);
void uninitialize_jar_path_tracing_module(ModuleInitializationLevel p_level);

#endif