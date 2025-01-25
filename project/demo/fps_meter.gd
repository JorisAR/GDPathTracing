extends Label

func _process(delta: float) -> void:
	text = "fps: " + str(round(1/ delta))
