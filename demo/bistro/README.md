# Bistro (not included)

`demo/demo/bistro_demo.tscn` uses the Amazon Lumberyard Bistro scene, which is too large for the
repository (1.7 GB; one FBX alone is over GitHub's 100 MB file limit). To run the demo:

1. Download **Bistro v5.2** from NVIDIA's Open Research Content Archive:
   <https://developer.nvidia.com/orca/amazon-lumberyard-bistro>
2. Unzip it here, so that this folder contains `BistroExterior.fbx`, `san_giuseppe_bridge_4k.hdr`
   and `Textures/`.
3. Open the project in Godot once so it imports the FBX files (takes a minute), then run
   `demo/bistro_demo.tscn`.

The demo uses `BistroExterior.fbx`, the HDR and the textures it references (base color, normal,
specular and emissive). The interior FBX files are not needed.

`demo/bistro_setup.gd` links the `_Specular` textures (roughness and metalness), turns on alpha
testing for cutout textures, and sets the FBX sun's energy. The camera uses the HDR at energy 10,
like Falcor's Bistro scene file.

## License and attribution

Bistro is licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) (see
`LICENSE.txt` in the download).

> Amazon Lumberyard Bistro, Open Research Content Archive (ORCA), 2017,
> <http://developer.nvidia.com/orca/amazon-lumberyard-bistro>
