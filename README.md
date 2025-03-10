# Godot Pathtracing Plugin

This project aims to add a basic Pathtracer to godot using compute shaders. It's primarily intended for educational and experimental purposes.

## Features

- **Pathtracer:** A software GPU-compute based path-tracer in Godot.
- **Scene Loading:** Give existing Godot scenes a beautiful makeover! Any meshinstance3Ds that are children of a ``GeometryGroup3D`` node will be processed for rendering using a ``PathTracingCamera``. 
- **Materials:** The pathtracer converts ``StandardMaterial3D`` into materials it uses to render the scene, to make rendering existing stuff as easy as possible!

    - Material support is rather limited, only supports albedo textures.



## Getting Started

### Installation

- Move the contents of the addons folder to the addons folder in your project.
- Ensure to compile using scons, or use a precompiled build.

## Usage

- Look at the setup in the demo scene for the required setup. Run the scene to start the process.

## Contributing

Contributions are welcome! Please fork the repository and submit a pull request.

Particular areas of interest:
- Support more texture types.
- Improve BVH quality.
- Add NEE/Direct light sampling.
- Sky HDRI.
- Simple post processing (e.g. bloom, controllable tone-mapping).
- Ability to update TLAS at runtime.
- Support for dynamic meshes.
- Denoiser.
- Transparent materials.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

- Special thanks to the Godot community for their support and resources.
- Gobot character: https://github.com/gdquest-demos/godot-4-3D-Characters
