extends Label
## Shows frame time and path tracer statistics. Tab cycles the debug view.

@export var camera : PathTracingCamera
@export var geometry_group : GeometryGroup3D

const DEBUG_NAMES = ["Path traced", "Normals", "Albedo", "Traversal cost"]


func _process(delta: float) -> void:
	var lines := PackedStringArray()
	lines.append("%d fps (%.2f ms)" % [Engine.get_frames_per_second(), delta * 1000.0])
	if camera:
		lines.append("view: %s  |  samples: %d  |  bounces: %d" % [DEBUG_NAMES[camera.debug_view], camera.get_sample_count(), camera.max_bounces])
	if geometry_group:
		lines.append("%d triangles, %d instances, %d emissive triangles" % [geometry_group.get_triangle_count(), geometry_group.get_instance_count(), geometry_group.get_emissive_triangle_count()])
	lines.append("WASD/QE move, Esc mouse, Space pause animation, Tab debug view")
	text = "\n".join(lines)


func _input(event: InputEvent) -> void:
	if camera and event.is_action_pressed("cycle_debug_view"):
		camera.debug_view = (camera.debug_view + 1) % DEBUG_NAMES.size()
