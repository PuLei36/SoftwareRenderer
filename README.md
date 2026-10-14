# Software Renderer

**English** | [简体中文](README.zh-CN.md)

A shader-based software renderer written from scratch in C89. No graphics API, everything rasterized on the CPU, running on Windows, macOS and Linux. On top of that it implements tile-based multithreaded rasterization.

Undergraduate final year project (Central South University of Forestry and Technology, Computer Science, 2026).

## Features

**Rendering pipeline**

- Programmable pipeline: vertex shader, fragment shader, generic varyings interpolation
- Homogeneous clipping (Sutherland-Hodgman, 7 planes)
- Back-face culling, perspective correct interpolation, early-Z depth testing
- Alpha testing (discard) and alpha blending
- Cubemapped skybox

**Shading and materials**

- Blinn-Phong reflection model
- PBR: Cook-Torrance BRDF (GGX + height-correlated Smith + Schlick)
- Both workflows: metallic-roughness and specular-glossiness
- IBL: diffuse irradiance + split-sum prefiltered specular + BRDF LUT
- Tangent space normal mapping, ambient occlusion, emission
- Shadow mapping with slope-scaled bias
- ACES tone mapping and gamma correction

**Everything else**

- Skeletal animation: linear blend skinning, quaternion slerp interpolation
- Orbital camera (orbit, pan, zoom)
- Asset loading: OBJ meshes, TGA and Radiance HDR images, text-based scene and animation files
- Reference-counted resource cache
- PBR material layer inspector
- Tile-based multithreaded rasterization with a cross-platform thread pool

## Rendering pipeline

`graphics_draw_triangle` handles one triangle, going through these stages in order:

**1. Vertex shading**　The vertex shader returns a clip space position and writes out the varyings.

**2. Homogeneous clipping**　A fast path first checks `|x|,|y|,|z| <= w` for all three vertices and skips clipping entirely when the triangle is fully visible. Otherwise Sutherland-Hodgman runs against 7 planes in turn: `w >= EPSILON` plus `±x`, `±y`, `±z`. The w plane keeps a 1e-5 margin so the later divide never hits a w of zero or below. Clipping happens before the perspective divide, so positions and varyings can both be lerped linearly with the same ratio. Each plane adds at most one vertex, which caps the polygon at 3+7=10 vertices.

**3. Triangle assembly**　The clipped polygon becomes a fan of `(0, i+1, i+2)` triangles.

**4. Primitive setup**　For each triangle: perspective divide to NDC, orientation test, store `1/w` per vertex, viewport transform, screen space bounding box.

Orientation is the 2D signed area in NDC space, counter-clockwise being front-facing (the OpenGL convention). Back faces are culled unless `double_sided` is set, in which case the backface flag is handed to the fragment shader, which flips the normal. The skybox relies on `double_sided` to never be culled.

**5. Rasterization**　Scan the bounding box pixel by pixel, sampling at the pixel center `(x+0.5, y+0.5)`. Barycentric weights come from solving `AP = s·AB + t·AC`; a pixel is covered when all three weights exceed `-EPSILON`.

**6. Depth testing**　Depth is window space z, `(NDC z + 1) / 2`, in [0,1], smaller being nearer, with the buffer cleared to 1. NDC z is linear in screen space, so depth interpolates with plain barycentric weights and needs no perspective correction. The test runs before the fragment shader (early-Z), and a discarded fragment writes no depth.

**7. Varyings interpolation**　Perspective correct interpolation multiplies each vertex's `1/w` by its barycentric weight, then normalizes to get the actual coefficients. A varyings block is treated as a flat float array and cast back to its own struct inside each shader, so the pipeline never needs to know the layout.

**8. Fragment shading and output**　Run the fragment shader (which may discard), saturate the result to [0,1], optionally alpha blend as `src·a + dst·(1-a)`, then write to the 8-bit color buffer and the float depth buffer.

### Frame order

Each frame updates models and skeletal animation first, then:

1. **Shadow pass** (when the scene enables it): sort by light-view distance, draw opaque models to depth only, copy the result into a shadow map texture.
2. **Main pass**: sort by camera distance, then draw opaque models front-to-back (letting early-Z skip shading), then the skybox, then transparent models back-to-front.

The skybox forces `clip.z` to `w·(1-EPSILON)`, pushing it to the far plane. Combined with the `<=` depth test it only fills pixels no opaque object covered.

## Shading

### Blinn-Phong

The final color sums emission, ambient and direct light:

```
emission + diffuse · ambient_intensity
         + [diffuse · (n·l) + specular · (n·h)^shininess] · punctual_intensity
```

Ambient is just the diffuse color times a constant intensity. Direct light comes from a single directional light and only contributes when `n·l > 0` and the fragment is not in shadow; the highlight uses the Blinn half vector `normalize(l+v)` and is computed only when a specular map exists. Textures on this path load as LDR color with no linearization, so lighting is computed in gamma space, and the output is not tone mapped.

### PBR

Direct light uses a Cook-Torrance microfacet BRDF:

- **D**　GGX / Trowbridge-Reitz normal distribution, with `alpha = roughness²`
- **V**　height-correlated Smith-GGX visibility, which already folds in `1/(4·n·l·n·v)`, making the specular lobe `F·V·D`
- **F**　Schlick approximation, with `F90` derived by scaling up the largest F0 component and saturating

These combine into `((1-F)·diffuse/π + F·V·D) · (n·l)`, contributing only when `n·l > 0` and `n·v > 0`.

**Both workflows**

Metallic-roughness: `diffuse = basecolor · 0.96 · (1-metalness)` and `F0 = lerp(0.04, basecolor, metalness)`. Specular-glossiness: specular is an RGB map, diffuse is scaled down by the largest specular component to conserve energy, and `roughness = 1 - glossiness`.

**IBL**

- **Diffuse**　sample a prebaked irradiance cubemap, multiplied by the diffuse color and AO.
- **Specular**　the split-sum approximation. The reflection direction is `2(n·v)n - v`; `roughness` picks the nearest of 10 prefiltered cubemap levels (no blending between levels), and the result is scaled by the BRDF LUT's scale and bias sampled at `(n·v, roughness)`.

Environment maps are generated offline with Filament's `cmgen`, driven by `scripts/utils/cmgen.py`.

**Normal mapping**　The vertex shader outputs world space T, B and N, where `bitangent = cross(N, T) · tangent.w`; the fragment shader transforms the tangent space normal through the TBN matrix.

**Tone mapping**　At the end of the fragment shader each channel goes through an ACES fit (Narkowicz) followed by a `pow(1/2.2)` gamma encode. Color maps on the PBR path are linearized at load time, while data maps (metalness, roughness, normal, AO) are left as-is.

### Shadow mapping

The light uses an orthographic projection looking from `-light_dir` toward the origin, and each scene's root transform scales the model to roughly the unit sphere to match that range. The default resolution is 512×512. The shadow pass fragment shader only performs the alpha cutoff, so in practice it writes depth alone.

Lookup compares the current depth against the shadow map, with a slope-scaled bias of `max(0.05·(1 - n·l), 0.005)`. Shadows mask direct light only, leaving IBL untouched. A single sample, so edges are hard.

## Skeletal animation

Animation data lives in text `.ani` files holding each joint's parent index, inverse bind matrix, and keyframe tracks for translation, rotation and scale. Translation and scale are lerped; rotation uses quaternion slerp along the shorter arc.

Each frame loops on `fmod(t, max_time)`, composes the local transform, walks the parent chain to accumulate the global transform, and multiplies by the inverse bind matrix to get the joint matrix. Vertices carry 4 joint indices and weights, and the vertex shader blends those 4 matrices by weight (linear blend skinning), with a separate inverse-transpose matrix for normals. A model can also be rigidly attached to a joint, which is how node animation works.

## Multithreaded rasterization

Most of the cost in a software renderer is in the fragment stage, but different triangles can cover the same pixel, so writing color and depth from several threads races, and locking per pixel costs more than it saves.

The approach here splits the screen into 16×16 tiles and **makes the tile the unit of work, with only one thread touching a given tile at a time**. Threads never write overlapping pixels, so no locking is needed at all; triangles within a tile are still processed in submission order, keeping depth testing and blending identical to single-threaded output. The varyings buffer used for interpolation is per worker thread, so no temporary data is shared.

There are two parallel paths:

- **Batched binning** (PBR, skybox): `graphics_begin_batch` opens a batch, each triangle finishes vertex processing and is registered into every tile its bounding box covers, then `graphics_end_batch` submits each non-empty tile as one task to the thread pool and waits.
- **Per-triangle splitting** (Blinn-Phong default): triangles whose bounding box covers at least 256 pixels are split into per-tile subtasks, while smaller ones are rasterized inline so scheduling never costs more than it gains. Blinn-Phong fragments are cheap enough that batching shows little benefit; set `RENDERER_BLINN_BATCH=1` to switch.

The thread pool (`core/threadpool.c`) is a fixed-size implementation: the task queue is a mutex-protected linked list with a counter of outstanding tasks, idle workers wait on a semaphore (Win32) or a condition variable (POSIX), and the last task to finish signals completion, which is what `threadpool_wait` uses as the per-batch sync point. The platform differences sit behind one set of internal functions.

In PBR scenes, successive iterations (an initial tile-parallel version, then simplified batch logic and handing triangle data ownership to the tasks themselves) took the frame rate from 27 FPS to 40 FPS. The program prints frame rate and average frame time to the console once a second, so running with 1 thread and with several makes for an easy comparison.

## Build

Requires CMake, plus Visual Studio (Windows), Xcode (macOS), or GCC/Clang and X11 (Linux).

```bash
# Windows: run from the project root
build_win32.bat

# or with CMake
mkdir build && cd build
cmake -D CMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release
```

On macOS and Linux use `build_macos.sh` or `build_linux.sh`.

## Usage

```
Viewer [test_name [scene_name]]
```

- **No arguments**　prompts for the thread count on the console (enter for the CPU core count, 1 for single-threaded), then opens a menu to pick the renderer and model.
- **`test_name` only** (`blinn` or `pbr`)　prompts for the thread count, then loads a random scene from that renderer.
- **Both arguments**　skips the prompt and loads directly, e.g. `Viewer pbr helmet` or `Viewer blinn azura`.

Environment variables:

| Variable | Effect |
| --- | --- |
| `RENDERER_THREADS` | Default thread count, replacing the CPU core count |
| `RENDERER_BLINN_BATCH` | Set to `1` to use batched binning for Blinn-Phong scenes |

### Controls

- Orbit: left mouse button
- Pan: right mouse button
- Zoom: mouse wheel
- Rotate lighting: <kbd>W</kbd> <kbd>A</kbd> <kbd>S</kbd> <kbd>D</kbd>
- Reset: <kbd>Space</kbd>
- Double click (PBR scenes): open the material layer inspector

## Code layout

```
renderer/
├── core/        math, graphics pipeline (graphics.c), thread pool (threadpool.c),
│                texture, image, mesh, skeleton, camera, scene, platform abstraction
├── shaders/     Blinn-Phong, PBR and skybox shaders, resource cache
├── scenes/      scene file parsing and per-model scene setup
├── tests/       main loop, startup menu, PBR layer inspector
├── platforms/   Win32 / macOS / Linux windowing and input
└── main.c       entry point
scripts/         asset preprocessing: glTF to extended OBJ, animation export, environment maps
```

The preprocessing scripts convert glTF models into extended OBJ files carrying tangents, joint indices and weights, and export `.ani` animations and scene files. Sources and licenses for the models and environment maps are in the READMEs under `assets/`.

## Credits and references

This project builds on the open source software renderer [zauonlok/renderer](https://github.com/zauonlok/renderer) (MIT).

- [ssloy/tinyrenderer](https://github.com/ssloy/tinyrenderer)　software rasterization tutorial
- [OpenGL ES 2.0 Specification](https://www.khronos.org/registry/OpenGL/specs/es/2.0/es_full_spec_2.0.pdf)　pipeline behavior, cubemap face selection, perspective correct interpolation
- [Homogeneous clipping](http://fabiensanglard.net/polygon_codec/)
- [Perspective correct interpolation](https://www.comp.nus.edu.sg/~lowkl/publications/lowk_persp_interp_techrep.pdf)
- [Physically Based Rendering in Filament](https://google.github.io/filament/Filament.html)　PBR and IBL
- [glTF 2.0 Sample Viewer](https://github.com/KhronosGroup/glTF-Sample-Viewer)　both PBR workflows
- Heitz, *Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs*　Smith visibility term
- [ACES filmic tone mapping curve](https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/)
- [three.js OrbitControls](https://github.com/mrdoob/three.js/blob/master/examples/js/controls/OrbitControls.js)　orbital camera

