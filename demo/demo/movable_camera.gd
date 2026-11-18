extends Node3D
## Free-fly camera. WASD to move, Q/E down/up, mouse wheel changes speed, Esc toggles mouse capture.
## Flying through the front of a linked Portal3D teleports the camera to the other side.

@export var move_speed : float = 8.0
const MIN_MOVE_SPEED = 0.5
const MAX_MOVE_SPEED = 512
var rotating : bool = false
var _portals : Array[Node] = []


func _ready() -> void:
	set_mouse(true)
	_portals = get_tree().root.find_children("*", "Portal3D", true, false)


func _process(delta : float) -> void:
	var wish := Input.get_vector("move_left", "move_right", "move_forward", "move_backward")
	var up := Vector3.ZERO
	if Input.is_action_pressed("move_up"):
		up = Vector3.UP
	if Input.is_action_pressed("move_down"):
		up = Vector3.DOWN
	var move_dir := transform.basis.z * wish.y + transform.basis.x * wish.x + up
	if move_dir != Vector3.ZERO:
		var from := global_position
		position += move_dir.normalized() * move_speed * delta / Engine.time_scale
		_cross_portals(from, global_position)


func _cross_portals(from: Vector3, to: Vector3) -> void:
	for portal in _portals:
		if portal.linked_portal and portal.is_crossing(from, to):
			global_transform = portal.get_teleport_transform() * global_transform
			return


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
