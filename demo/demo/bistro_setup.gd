extends Node
## Bistro demo helper. The Bistro FBX files ship analytic lights meant for raster rendering; the
## scene is lit by its emissive surfaces when path traced, so those lights are hidden unless
## `use_fbx_lights` is on. The Bistro files are not in the repository: see demo/bistro/README.md.

@export var use_fbx_lights := false
## When the FBX lights are used, their energy is replaced by this (the FBX values are for Falcor).
## Negative keeps the FBX value.
@export var fbx_light_energy := -1.0
## Bistro's materials as imported from FBX are fully rough: the "_Specular" textures
## (red occlusion, green roughness, blue metalness) are not linked. Foliage imports as alpha depth
## prepass (stochastic alpha in the path tracer).
## With this on, the textures are linked and base colors that can carry alpha (DXT5) get alpha scissor.
@export var fix_materials := true
## Multiplies the emission of every emissive material. Falcor's Bistro scene files scale the
## interior's emissives by 1000 for the same reason: the FBX values are far too dim.
@export var emission_scale := 1000.0


func _enter_tree() -> void:
	# Before GeometryGroup3D._ready() collects the lights.
	get_tree().node_added.connect(_on_node_added)


func _on_node_added(node: Node) -> void:
	if node is Light3D and get_parent().is_ancestor_of(node):
		if not use_fbx_lights:
			(node as Light3D).visible = false
		elif fbx_light_energy >= 0.0:
			(node as Light3D).light_energy = fbx_light_energy


func _ready() -> void:
	# Runs before the GeometryGroup3D sibling that follows this node builds the scene.
	get_tree().node_added.disconnect(_on_node_added)
	var done := {}
	for mi: MeshInstance3D in get_parent().find_children("*", "MeshInstance3D", true, false):
		for s in mi.get_surface_override_material_count():
			var mat := mi.get_active_material(s) as BaseMaterial3D
			if mat == null or done.has(mat):
				continue
			done[mat] = true
			if mat.emission_enabled:
				mat.emission_energy_multiplier *= emission_scale
			if fix_materials:
				_fix_material(mat)


func _fix_material(mat: BaseMaterial3D) -> void:
	var albedo := mat.albedo_texture
	if albedo == null or not albedo.resource_path.ends_with("_BaseColor.dds"):
		return
	var spec_path := albedo.resource_path.replace("_BaseColor.dds", "_Specular.dds")
	if ResourceLoader.exists(spec_path):
		var spec := load(spec_path) as Texture2D
		mat.roughness = 1.0
		mat.roughness_texture = spec
		mat.roughness_texture_channel = BaseMaterial3D.TEXTURE_CHANNEL_GREEN
		mat.metallic = 1.0
		mat.metallic_texture = spec
		mat.metallic_texture_channel = BaseMaterial3D.TEXTURE_CHANNEL_BLUE
	var img := albedo.get_image()
	if img and img.get_format() in [Image.FORMAT_DXT3, Image.FORMAT_DXT5, Image.FORMAT_BPTC_RGBA]:
		mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
		mat.alpha_scissor_threshold = 0.5
