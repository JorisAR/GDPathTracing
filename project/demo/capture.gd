extends Node
## Development helper: when run with `-- --capture=<path.png> [--capture-frames=N]`, saves a
## screenshot after N frames and quits. Used for quick visual regression checks.

var _path := ""
var _frames := 120
var _elapsed := 0.0
var _counted := 0


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
		elif arg == "--static":
			# Freeze animations so accumulation can converge.
			for node in get_tree().get_nodes_in_group("animated"):
				node.set_process(false)
	if _path.is_empty():
		queue_free()


func _process(delta: float) -> void:
	_frames -= 1
	if _counted >= 0 and Engine.get_process_frames() > 10:
		_elapsed += delta
		_counted += 1
	if _frames > 0:
		return
	if _counted > 0:
		print("[capture] average frame time %.2f ms over %d frames" % [_elapsed / _counted * 1000.0, _counted])
	var image := get_viewport().get_texture().get_image()
	image.save_png(_path)
	print("[capture] saved ", _path)
	get_tree().quit()
