extends Label
## Shows frame time and path tracer statistics. Tab cycles the debug view.

@export var camera : PathTracingCamera
@export var geometry_group : GeometryGroup3D

const DEBUG_NAMES = ["Path traced", "Normals", "Albedo", "Traversal cost"]


func _process(delta: float) -> void:
	var lines := PackedStringArray()
	lines.append("%d fps (%.2f ms)" % [Engine.get_frames_per_second(), delta * 1000.0])
	if camera:
		lines.append("%s  |  view: %s  |  samples: %d  |  bounces: %d  |  temporal: %s  |  ReSTIR GI: %s  |  denoise: %s" % [camera.get_active_backend(), DEBUG_NAMES[camera.debug_view], camera.get_sample_count(), camera.max_bounces, "on" if camera.temporal_reprojection else "off", "on" if camera.restir_gi else "off", "on" if camera.denoise else "off"])
	if geometry_group:
		lines.append("%d triangles, %d instances, %d emissive triangles" % [geometry_group.get_triangle_count(), geometry_group.get_instance_count(), geometry_group.get_emissive_triangle_count()])
	lines.append("WASD/QE move, Esc mouse, Space pause animation, Tab debug view, B backend, T temporal, R ReSTIR GI, N denoise")
	text = "\n".join(lines)


func _input(event: InputEvent) -> void:
	if camera and event.is_action_pressed("cycle_debug_view"):
		camera.debug_view = (camera.debug_view + 1) % DEBUG_NAMES.size()
	if camera and event is InputEventKey and event.pressed and not event.echo:
		if event.physical_keycode == KEY_B:
			# Auto -> Software -> Hardware -> Auto
			camera.backend = (camera.backend + 1) % 3
		elif event.physical_keycode == KEY_T:
			camera.temporal_reprojection = not camera.temporal_reprojection
		elif event.physical_keycode == KEY_R:
			camera.restir_gi = not camera.restir_gi
		elif event.physical_keycode == KEY_N:
			camera.denoise = not camera.denoise
