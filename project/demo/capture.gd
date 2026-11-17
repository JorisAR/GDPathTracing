extends Node
## Development helper: when run with `-- --capture=<path.png> [--capture-frames=N]`, saves a
## screenshot after N frames and quits. Used for quick visual regression checks.

var _path := ""
var _frames := 120
var _elapsed := 0.0
var _counted := 0
var _gpu_elapsed := 0.0
var _pan := false
var _pan_time := 0.0
var _walk := false


func _ready() -> void:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--capture="):
			_path = arg.trim_prefix("--capture=")
		elif arg.begins_with("--capture-frames="):
			_frames = int(arg.trim_prefix("--capture-frames="))
		elif arg == "--raster":
			# Compare against Godot's rasterizer.
			var cam := get_viewport().get_camera_3d()
			if cam and "path_tracing_enabled" in cam:
				cam.path_tracing_enabled = false
		elif arg.begins_with("--debug-view="):
			var cam := get_viewport().get_camera_3d()
			if cam and "debug_view" in cam:
				cam.debug_view = int(arg.trim_prefix("--debug-view="))
		elif arg.begins_with("--backend="):
			var cam := get_viewport().get_camera_3d()
			if cam and "backend" in cam:
				cam.backend = int(arg.trim_prefix("--backend="))
		elif arg.begins_with("--camera="):
			# --camera=x,y,z,yaw,pitch (degrees): place the camera for a screenshot.
			var v := arg.trim_prefix("--camera=").split_floats(",")
			var cam := get_viewport().get_camera_3d()
			if cam and v.size() == 5:
				cam.global_position = Vector3(v[0], v[1], v[2])
				cam.rotation_degrees = Vector3(v[4], v[3], 0)
		elif arg == "--no-denoise":
			var cam := get_viewport().get_camera_3d()
			if cam and "denoise" in cam:
				cam.denoise = false
		elif arg == "--no-temporal":
			var cam := get_viewport().get_camera_3d()
			if cam and "temporal_reprojection" in cam:
				cam.temporal_reprojection = false
		elif arg == "--pan":
			# Sway the camera to exercise temporal reprojection.
			_pan = true
		elif arg == "--walk":
			# Hold "forward" until the camera has teleported through a portal (portal_demo).
			_walk = true
			Input.action_press("move_forward")
		elif arg == "--static":
			# Freeze animations so accumulation can converge.
			for node in get_tree().get_nodes_in_group("animated"):
				node.set_process(false)
	if _path.is_empty():
		queue_free()
		return
	# Scripted runs: ignore the real mouse so it cannot turn the camera mid-capture.
	var capture_cam := get_viewport().get_camera_3d()
	if capture_cam and capture_cam.has_method("set_mouse"):
		capture_cam.call_deferred("set_mouse", false)
	RenderingServer.viewport_set_measure_render_time(get_viewport().get_viewport_rid(), true)


func _process(delta: float) -> void:
	_frames -= 1
	if _pan:
		_pan_time += delta
		var cam := get_viewport().get_camera_3d()
		if cam:
			cam.position.x = sin(_pan_time * 0.8) * 1.2
			cam.rotation.y = sin(_pan_time * 0.8) * 0.15
	if _counted >= 0 and Engine.get_process_frames() > 10:
		_elapsed += delta
		_gpu_elapsed += RenderingServer.viewport_get_measured_render_time_gpu(get_viewport().get_viewport_rid())
		_counted += 1
	if _walk:
		var cam := get_viewport().get_camera_3d()
		if cam and cam.global_position.z < -10.0:
			Input.action_release("move_forward")
			print("[capture] walked through a portal, camera now at ", cam.global_position.snappedf(0.01), " facing ", (-cam.global_basis.z).snappedf(0.01))
			_walk = false
	if _frames > 0:
		return
	if _counted > 0:
		print("[capture] average frame time %.2f ms (GPU %.2f ms) over %d frames" % [_elapsed / _counted * 1000.0, _gpu_elapsed / _counted, _counted])
	var image := get_viewport().get_texture().get_image()
	image.save_png(_path)
	print("[capture] saved ", _path)
	get_tree().quit()
