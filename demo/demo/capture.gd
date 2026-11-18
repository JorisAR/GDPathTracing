extends Node
## Development helper: when run with `-- --capture=<path.png> [--capture-frames=N]`, saves a
## screenshot after N frames and quits. Used for quick visual regression checks.

var _pan_origin = null # [position, rotation, right vector] when the sway started
var _path := ""
var _frames := 120
var _elapsed := 0.0
var _counted := 0
var _gpu_elapsed := 0.0
var _pan := false
var _pan_time := 0.0
var _walk := false


func _enter_tree() -> void:
	# Group properties must be set before GeometryGroup3D._ready() builds the scene. The whole scene
	# has entered the tree by now, but no node is ready yet.
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--group-set="):
			var kv := arg.trim_prefix("--group-set=").split("=", true, 1)
			if kv.size() == 2:
				for group in get_parent().find_children("*", "GeometryGroup3D", true, false):
					group.set(kv[0], str_to_var(kv[1]))


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
		elif arg.begins_with("--set="):
			# --set=property=value on the camera, value parsed with str_to_var (e.g. --set=restir_neighbors=0).
			var kv := arg.trim_prefix("--set=").split("=", true, 1)
			var cam := get_viewport().get_camera_3d()
			if cam and kv.size() == 2:
				cam.set(kv[0], str_to_var(kv[1]))
		elif arg.begins_with("--group-set="):
			# --group-set=property=value on every GeometryGroup3D (before it builds: set in _enter_tree).
			pass
		elif arg.begins_with("--env="):
			# --env=res://path.hdr[,energy]: environment map on the camera.
			var parts := arg.trim_prefix("--env=").split(",")
			var cam := get_viewport().get_camera_3d()
			if cam and "environment_map" in cam:
				cam.environment_map = load(parts[0])
				if parts.size() > 1:
					cam.environment_energy = float(parts[1])
		elif arg == "--no-accumulate":
			var cam := get_viewport().get_camera_3d()
			if cam and "accumulate" in cam:
				cam.accumulate = false
		elif arg == "--no-restir":
			var cam := get_viewport().get_camera_3d()
			if cam and "restir_gi" in cam:
				cam.restir_gi = false
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
		# A click would capture the mouse again, and keys would move the camera: no live input at all.
		capture_cam.set_process_input(false)
		if not _walk:
			capture_cam.set_process(false)
	RenderingServer.viewport_set_measure_render_time(get_viewport().get_viewport_rid(), true)


func _process(delta: float) -> void:
	_frames -= 1
	if _pan:
		# Fixed step, so the camera path does not depend on the frame rate (comparable timings).
		_pan_time += 1.0 / 60.0
		var cam := get_viewport().get_camera_3d()
		if cam:
			if _pan_origin == null:
				_pan_origin = [cam.position, cam.rotation, cam.basis.x]
			cam.position = _pan_origin[0] + _pan_origin[2] * sin(_pan_time * 0.8) * 1.2
			cam.rotation.y = _pan_origin[1].y + sin(_pan_time * 0.8) * 0.15
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
	_accumulate_pass_times()
	if _frames > 0:
		return
	_print_pass_times()
	if _counted > 0:
		print("[capture] average frame time %.2f ms (GPU %.2f ms) over %d frames" % [_elapsed / _counted * 1000.0, _gpu_elapsed / _counted, _counted])
	var image := get_viewport().get_texture().get_image()
	image.save_png(_path)
	print("[capture] saved ", _path)
	get_tree().quit()


var _pass_ms := {}
var _pass_frames := 0


## Per-pass GPU times from the "PT ..." timestamps the path tracing effect records (last frame).
func _accumulate_pass_times() -> void:
	if Engine.get_process_frames() <= 10:
		return
	var rd := RenderingServer.get_rendering_device()
	var n := rd.get_captured_timestamps_count()
	var prev := -1
	for i in n:
		var name := rd.get_captured_timestamp_name(i)
		if not name.begins_with("PT "):
			continue
		var t := rd.get_captured_timestamp_gpu_time(i)
		if prev >= 0 and name != "PT begin":
			_pass_ms[name] = _pass_ms.get(name, 0.0) + (t - prev) / 1e6
		prev = t
	_pass_frames += 1


func _print_pass_times() -> void:
	if _pass_frames == 0 or _pass_ms.is_empty():
		return
	var total := 0.0
	for k in _pass_ms:
		print("[capture]   %-24s %6.2f ms" % [k.trim_prefix("PT "), _pass_ms[k] / _pass_frames])
		total += _pass_ms[k] / _pass_frames
	print("[capture]   %-24s %6.2f ms" % ["path tracer total", total])
