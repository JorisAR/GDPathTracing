#include "register_types.h"
#include "path_tracing/path_tracing_camera.h"
#include "path_tracing/geometry_group3d.h"

using namespace godot;

void initialize_jar_path_tracing_module(ModuleInitializationLevel p_level)
{
    if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE)
    {
        GDREGISTER_CLASS(PathTracingCamera)
        GDREGISTER_CLASS(GeometryGroup3D)

    }
}

void uninitialize_jar_path_tracing_module(ModuleInitializationLevel p_level)
{
    if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE)
    {
        
    }
}

extern "C"
{
    // Initialization.
    GDExtensionBool GDE_EXPORT jar_path_tracing_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address,
                                                                const GDExtensionClassLibraryPtr p_library,
                                                                GDExtensionInitialization *r_initialization)
    {
        godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

        init_obj.register_initializer(initialize_jar_path_tracing_module);
        init_obj.register_terminator(uninitialize_jar_path_tracing_module);
        init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

        return init_obj.init();
    }
}