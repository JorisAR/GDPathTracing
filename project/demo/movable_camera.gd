extends Node3D
## Free-fly camera. WASD to move, Q/E down/up, mouse wheel changes speed, Esc toggles mouse capture.

@export var move_speed : float = 8.0
const MIN_MOVE_SPEED = 0.5
const MAX_MOVE_SPEED = 512
var rotating : bool = false


func _ready() -> void:
	set_mouse(true)


func _process(delta : float) -> void:
	var wish := Input.get_vector("move_left", "move_right", "move_forward", "move_backward")
	var up := Vector3.ZERO
	if Input.is_action_pressed("move_up"):
		up = Vector3.UP
	if Input.is_action_pressed("move_down"):
		up = Vector3.DOWN
	var move_dir := transform.basis.z * wish.y + transform.basis.x * wish.x + up
	if move_dir != Vector3.ZERO:
		position += move_dir.normalized() * move_speed * delta / Engine.time_scale


func _input(event) -> void:
	if event.is_action_pressed("scroll_up"):
		move_speed *= 2
	if event.is_action_pressed("scroll_down"):
		move_speed *= 0.5
	move_speed = clamp(move_speed, MIN_MOVE_SPEED, MAX_MOVE_SPEED)
	if event.is_action_pressed("toggle_mouse"):
		set_mouse(not rotating)
	if event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_LEFT and not rotating:
		set_mouse(true)
	if event is InputEventMouseMotion and rotating:
		rotation += Vector3(-event.relative.y * 0.0025, -event.relative.x * 0.0025, 0)
		rotation.x = clamp(rotation.x, -PI / 2.2, PI / 2.2)


func set_mouse(value: bool) -> void:
	rotating = value
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED if rotating else Input.MOUSE_MODE_VISIBLE
