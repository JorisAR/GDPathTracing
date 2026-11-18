"""Generates demo/demo/portal_demo.tscn and its checker texture.

Usage: python tools/gen_portal_demo.py [project_dir]   (default: ./demo). Needs Pillow.
The scene is generated because it is mostly boxes and portal frames; edit this script, not the
.tscn, when changing the layout.
"""
import sys, os
from PIL import Image

project = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "demo")

# ---------------------------------------------------------------- checker texture
tex_path = os.path.join(project, 'demo', 'materials', 'checker.png')
img = Image.new('RGB', (1024, 1024))
px = img.load()
for y in range(1024):
    for x in range(1024):
        px[x, y] = (225, 222, 215) if ((x // 128) + (y // 128)) % 2 == 0 else (60, 62, 70)
img.save(tex_path)

# ---------------------------------------------------------------- helpers
def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])

def basis(z, y):
    """Columns X, Y, Z with Z = forward (portal front), Y = up."""
    return (cross(y, z), y, z)

IDENTITY = ((1, 0, 0), (0, 1, 0), (0, 0, 1))

def fmt(v):
    v = round(v, 5)
    return str(int(v)) if v == int(v) else repr(v)

def xform(cols=IDENTITY, origin=(0, 0, 0), scale=1.0):
    X, Y, Z = cols
    rows = [(X[i] * scale, Y[i] * scale, Z[i] * scale) for i in range(3)]
    vals = [v for r in rows for v in r] + list(origin)
    return 'Transform3D(' + ', '.join(fmt(v) for v in vals) + ')'

materials = {}  # name -> body
meshes = {}     # size -> id
nodes = []

def material(name, body):
    materials[name] = body
    return name

def box_mesh(size):
    key = tuple(round(s, 4) for s in size)
    if key not in meshes:
        meshes[key] = 'Box_%d' % len(meshes)
    return meshes[key]

def node(name, type_, parent, props, extra=''):
    nodes.append((name, type_, parent, props, extra))

def box(name, parent, size, origin, mat, cols=IDENTITY, extra_props=None):
    props = ['transform = ' + xform(cols, origin), 'mesh = SubResource("%s")' % box_mesh(size),
             'surface_material_override/0 = SubResource("%s")' % mat]
    if extra_props:
        props += extra_props
    node(name, 'MeshInstance3D', parent, props)

def portal(name, parent, cols, origin, size, link, tint, frame_mat, depth=0.08, bar=0.12):
    w, h = size
    props = ['transform = ' + xform(cols, origin), 'size = Vector2(%s, %s)' % (fmt(w), fmt(h)),
             'linked_portal = NodePath("%s")' % link]
    if tint is not None:
        props.append('tint = Color(%s, %s, %s, 1)' % tuple(fmt(c) for c in tint))
    node(name, 'Portal3D', parent, props, ' node_paths=PackedStringArray("linked_portal")')
    # Frame bars in portal-local space, slightly proud of the portal plane.
    me = parent + '/' + name
    bars = [
        ('Top', (w + 2 * bar, bar, depth), (0, h / 2 + bar / 2, 0)),
        ('Bottom', (w + 2 * bar, bar, depth), (0, -h / 2 - bar / 2, 0)),
        ('Left', (bar, h, depth), (-w / 2 - bar / 2, 0, 0)),
        ('Right', (bar, h, depth), (w / 2 + bar / 2, 0, 0)),
    ]
    for bname, bsize, borigin in bars:
        box('Frame' + bname, me, bsize, borigin, frame_mat)

# ---------------------------------------------------------------- materials
M_WALL = material('Mat_wall', 'albedo_color = Color(0.82, 0.8, 0.76, 1)\nroughness = 0.85')
M_FLOOR = material('Mat_checker', 'albedo_texture = ExtResource("tex_checker")\nuv1_scale = Vector3(3, 3, 3)\nroughness = 0.6')
M_ACCENT = material('Mat_accent', 'albedo_color = Color(0.25, 0.4, 0.75, 1)\nroughness = 0.7')
M_ORANGE = material('Mat_orange', 'albedo_color = Color(0.9, 0.42, 0.15, 1)\nroughness = 0.5')
M_GOLD = material('Mat_gold', 'albedo_color = Color(1, 0.78, 0.35, 1)\nmetallic = 1.0\nroughness = 0.25')
M_CHROME = material('Mat_chrome', 'albedo_color = Color(0.9, 0.9, 0.92, 1)\nmetallic = 1.0\nroughness = 0.08')
M_GRASS = material('Mat_grass', 'albedo_texture = ExtResource("tex_grass")\nuv1_scale = Vector3(10, 10, 10)')
M_STONE = material('Mat_stone', 'albedo_color = Color(0.55, 0.53, 0.5, 1)\nroughness = 0.9')
M_RED = material('Mat_red', 'albedo_color = Color(0.75, 0.12, 0.1, 1)\nroughness = 0.6')
M_LIGHT = material('Mat_light', 'albedo_color = Color(0.1, 0.1, 0.1, 1)\nemission_enabled = true\nemission = Color(1, 0.9, 0.75, 1)\nemission_energy_multiplier = 7.0')
M_FRAME_VIOLET = material('Mat_frame_violet', 'albedo_color = Color(0.1, 0.1, 0.1, 1)\nemission_enabled = true\nemission = Color(0.65, 0.35, 1, 1)\nemission_energy_multiplier = 2.5')
M_FRAME_GOLD = material('Mat_frame_gold', 'albedo_color = Color(0.1, 0.1, 0.1, 1)\nemission_enabled = true\nemission = Color(1, 0.7, 0.25, 1)\nemission_energy_multiplier = 2.5')
M_FRAME_WHITE = material('Mat_frame_white', 'albedo_color = Color(0.9, 0.9, 0.9, 1)\nroughness = 0.4')

# ---------------------------------------------------------------- hall (12 x 4 x 12, centred on the origin)
G = 'GeometryGroup3D'
HALL = G + '/Hall'
node('Hall', 'Node3D', G, [])
S, H = 6.0, 4.0
box('Floor', HALL, (2 * S + 0.4, 0.2, 2 * S + 0.4), (0, -0.1, 0), M_FLOOR)
box('Ceiling', HALL, (2 * S + 0.4, 0.2, 2 * S + 0.4), (0, H + 0.1, 0), M_WALL)
box('WallLeft', HALL, (0.2, H + 0.4, 2 * S), (-S - 0.1, H / 2, 0), M_WALL)
box('WallRight', HALL, (0.2, H + 0.4, 2 * S), (S + 0.1, H / 2, 0), M_WALL)
box('WallBack', HALL, (2 * S + 0.4, H + 0.4, 0.2), (0, H / 2, -S - 0.1), M_ACCENT)
box('WallFront', HALL, (2 * S + 0.4, H + 0.4, 0.2), (0, H / 2, S + 0.1), M_WALL)
box('LightA', HALL, (1.6, 0.04, 1.6), (-3, H - 0.02, 0), M_LIGHT)
box('LightB', HALL, (1.6, 0.04, 1.6), (0, H - 0.02, 3.5), M_LIGHT)

box('Pedestal', HALL, (0.7, 1.0, 0.7), (-2, 0.5, -1.5), M_STONE)
node('Suzanne', 'MeshInstance3D', HALL, [
    'transform = ' + xform(IDENTITY, (-2, 1.45, -1.5), 0.5), 'mesh = ExtResource("mesh_suzanne")',
    'surface_material_override/0 = SubResource("%s")' % M_GOLD, 'script = ExtResource("script_anim")',
    'spin_speed = Vector3(0, 0.6, 0)'])
box('Crate', HALL, (0.8, 0.8, 0.8), (2.2, 0.4, -3.2), M_ORANGE,
    cols=((0.866, 0, -0.5), (0, 1, 0), (0.5, 0, 0.866)))
node('Ball', 'MeshInstance3D', HALL, [
    'transform = ' + xform(IDENTITY, (-3.5, 0.5, 3)), 'mesh = SubResource("Sphere")',
    'surface_material_override/0 = SubResource("%s")' % M_CHROME])

# Endless corridor: opposite side walls of the same room.
UP = (0, 1, 0)
portal('CorridorWest', HALL, basis((1, 0, 0), UP), (-S + 0.01, 1.4, 0), (1.8, 2.6), '../CorridorEast',
       (0.85, 0.82, 0.95), M_FRAME_VIOLET)
portal('CorridorEast', HALL, basis((-1, 0, 0), UP), (S - 0.01, 1.4, 0), (1.8, 2.6), '../CorridorWest',
       (0.85, 0.82, 0.95), M_FRAME_VIOLET)
# Fall loop: floor -> ceiling of the same room.
portal('FloorHole', HALL, basis((0, 1, 0), (0, 0, -1)), (3, 0.01, 2), (2, 2), '../CeilingHole',
       None, M_FRAME_GOLD, depth=0.04)
portal('CeilingHole', HALL, basis((0, -1, 0), (0, 0, -1)), (3, H - 0.01, 2), (2, 2), '../FloorHole',
       None, M_FRAME_GOLD, depth=0.04)
# Door to the garden on the back wall.
portal('GardenDoor', HALL, basis((0, 0, 1), UP), (0, 1.3, -S + 0.01), (1.6, 2.6),
       '../../Garden/GardenExit', None, M_FRAME_WHITE)

# ---------------------------------------------------------------- garden (far away, open sky + sun)
GARDEN = G + '/Garden'
node('Garden', 'Node3D', G, ['transform = ' + xform(basis((0, 0, -1), UP), (0, 0, -80))])  # faces away from the hall
node('Ground', 'MeshInstance3D', GARDEN, ['mesh = SubResource("Ground")',
                                          'surface_material_override/0 = SubResource("%s")' % M_GRASS])
portal('GardenExit', GARDEN, basis((0, 0, 1), UP), (0, 1.3, 0), (1.6, 2.6), '../../Hall/GardenDoor',
       None, M_FRAME_WHITE, depth=0.2)
for i, (x, z, h) in enumerate([(-4, 6, 2.5), (4, 7, 3.5), (-6, 12, 4.5), (5, 14, 2.0), (0, 18, 6.0)]):
    box('Pillar%d' % i, GARDEN, (0.7, h, 0.7), (x, h / 2, z), M_STONE)
box('RedBlock', GARDEN, (1.2, 1.2, 1.2), (-1.8, 0.6, 9), M_RED, cols=((0.8, 0, -0.6), (0, 1, 0), (0.6, 0, 0.8)))
node('GoldBall', 'MeshInstance3D', GARDEN, [
    'transform = ' + xform(IDENTITY, (1.8, 0.8, 4.5), 1.6), 'mesh = SubResource("Sphere")',
    'surface_material_override/0 = SubResource("%s")' % M_GOLD, 'script = ExtResource("script_anim")',
    'spin_speed = Vector3(0, 0, 0)', 'bob_amplitude = Vector3(0, 0.3, 0)', 'bob_speed = 1.3'])
node('Sun', 'DirectionalLight3D', GARDEN, [
    'transform = ' + xform(basis((0.45, 0.75, 0.48), (0, 0.54, -0.84)), (0, 10, 0)),
    'light_color = Color(1, 0.93, 0.8, 1)', 'light_energy = 1.6', 'light_angular_distance = 1.5',
    'shadow_enabled = true'])

# ---------------------------------------------------------------- write
out = ['[gd_scene format=3]', '',
       '[ext_resource type="Script" path="res://demo/movable_camera.gd" id="script_cam"]',
       '[ext_resource type="Script" path="res://demo/fps_meter.gd" id="script_fps"]',
       '[ext_resource type="Script" path="res://demo/animator.gd" id="script_anim"]',
       '[ext_resource type="Script" path="res://demo/capture.gd" id="script_capture"]',
       '[ext_resource type="Texture2D" path="res://demo/materials/grass_wales_diffuse.png" id="tex_grass"]',
       '[ext_resource type="Texture2D" path="res://demo/materials/checker.png" id="tex_checker"]',
       '[ext_resource type="ArrayMesh" path="res://demo/geometry/suzanne.obj" id="mesh_suzanne"]', '',
       '[sub_resource type="Environment" id="Environment_1"]', 'background_mode = 1',
       'background_color = Color(0.5, 0.65, 0.9, 1)', 'tonemap_mode = 3', '']
for name, body in materials.items():
    out += ['[sub_resource type="StandardMaterial3D" id="%s"]' % name, body, '']
for size, mid in meshes.items():
    out += ['[sub_resource type="BoxMesh" id="%s"]' % mid, 'size = Vector3(%s, %s, %s)' % tuple(fmt(s) for s in size), '']
out += ['[sub_resource type="SphereMesh" id="Sphere"]', '',
        '[sub_resource type="PlaneMesh" id="Ground"]', 'size = Vector2(60, 60)', '']

out += ['[node name="PortalDemo" type="Node3D"]', '',
        '[node name="Camera3D" type="PathTracingCamera" parent="." node_paths=PackedStringArray("geometry_group")]',
        'transform = ' + xform(IDENTITY, (0, 1.7, 5)), 'fov = 75.0', 'geometry_group = NodePath("../GeometryGroup3D")',
        'max_bounces = 4', 'firefly_clamp = 20.0', 'max_portal_hops = 12', 'sky_energy = 0.9',
        'script = ExtResource("script_cam")', 'move_speed = 3.0', '',
        '[node name="WorldEnvironment" type="WorldEnvironment" parent="."]',
        'environment = SubResource("Environment_1")', '',
        '[node name="GeometryGroup3D" type="GeometryGroup3D" parent="."]', '']
for name, type_, parent, props, extra in nodes:
    out += ['[node name="%s" type="%s" parent="%s"%s]' % (name, type_, parent, extra)] + props + ['']
out += ['[node name="Label" type="Label" parent="." node_paths=PackedStringArray("camera", "geometry_group")]',
        'offset_left = 8.0', 'offset_top = 8.0', 'offset_right = 48.0', 'offset_bottom = 31.0',
        'script = ExtResource("script_fps")', 'camera = NodePath("../Camera3D")',
        'geometry_group = NodePath("../GeometryGroup3D")', '',
        '[node name="Capture" type="Node" parent="."]', 'script = ExtResource("script_capture")', '']
open(os.path.join(project, 'demo', 'portal_demo.tscn'), 'w', newline='\n').write('\n'.join(out))
print('nodes:', len(nodes), 'materials:', len(materials), 'meshes:', len(meshes))
