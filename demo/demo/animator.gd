extends Node3D
## Simple motion to exercise dynamic instances (per-frame TLAS rebuilds). Space toggles.

@export var spin_speed : Vector3 = Vector3(0, 0.6, 0)
@export var bob_amplitude : Vector3 = Vector3.ZERO
@export var bob_speed : float = 1.0

static var paused := false
var _time := 0.0
var _origin : Vector3


func _ready() -> void:
	_origin = position
	add_to_group("animated")


func _process(delta: float) -> void:
	if paused:
		return
	_time += delta
	# Rotate around the parent's axes so tilted/scaled meshes spin without Euler wobble.
	if spin_speed != Vector3.ZERO:
		rotate(spin_speed.normalized(), spin_speed.length() * delta)
	position = _origin + bob_amplitude * sin(_time * bob_speed)


func _unhandled_input(event: InputEvent) -> void:
	if event.is_action_pressed("toggle_animation"):
		paused = not paused
		get_viewport().set_input_as_handled()
