# JAR Voxel Terrain

This project aims to add a basic Pathtracer to godot using compute shaders. It's primarily intended for educational and experimental purposes.

## Features

- **Pathtracer:** A software compute based raytracer, ensuring compatability with any device.
- **Scene Loading:** Give existing Godot scenes a beautiful makeover! Any meshinstances that are children of a ``GeometryGroup3D`` node will be processed for rendering using a ``PathTracingCamera``. 
- **Materials:** The pathtracer converts ``StandardMaterial3D`` into materials it uses to render the scene, to make rendering existing stuff as easy as possible!

### Planned: (not in order)
- Lighting
- Post Processing
- Denoiser
- Skinned meshes
- Dynamic Scenes (and use TLAS)
- Textures
- Skybox/HDRI
- Transparency


## Getting Started

### Installation

- move the contents of the addons folder to the addons folder in your project.
- ensure to compile using scons, or use a precompiled build.

## Usage

## Contributing

Contributions are welcome! Please fork the repository and submit a pull request.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

- Special thanks to the Godot community for their support and resources.
