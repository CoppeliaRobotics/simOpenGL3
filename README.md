# simOpenGL3 (modern)

A shader based external renderer plugin for CoppeliaSim, meant as a drop-in
replacement for the legacy `simOpenGL3` renderer in `../sourceCode`. It speaks
exactly the same protocol as the legacy plugin:

* the plugin is called `simOpenGL3`, so CoppeliaSim's *OpenGL3* render mode
  (`sim_rendermode_opengl3`) picks it up;
* it exports `simOpenGL3Renderer`, the symbol `CPluginContainer` resolves for
  that render mode;
* it consumes the `sim_message_eventcallback_extrenderer_start/light/mesh/stop`
  message sequence with the same `void* data[]` layout.

Nothing has to change on the engine side: build it, drop `simOpenGL3.dll` where
the old one was, done.

---

## What is actually different

The legacy renderer already used shaders and a framebuffer object, so
"modernise" here means replacing the practices that stop it from scaling, not
just porting to a newer GLSL version.

### Off-screen rendering

| legacy | this plugin |
|---|---|
| one `QOpenGLContext` + `QOffscreenSurface` **per camera and per vision sensor** | **one** context for the whole plugin |
| one shader program, one mesh cache, one texture cache **per context** | one program set and one cache set, shared by every view |
| `QOpenGLFramebufferObject` (RGBA8, 24 bit depth, no MSAA) | multisample `RGBA16F` + `DEPTH_COMPONENT32F` target, explicit `glBlitFramebuffer` resolve of **colour and depth**, then a tone-map pass into an 8 bit target |
| `glReadPixels` straight into CoppeliaSim's buffer | `glReadPixels` into a pixel-pack buffer, then a single `memcpy` out of the mapping |
| context shared with the GUI widget, selected by `sim_boolparam_qglwidget` | no sharing at all: nothing from the Qt widget is ever used as a GL resource, so the (now removed) parameter is not needed |
| contexts leaked on shutdown, GL objects deleted from destructors under whatever context happened to be current | every GL object is destroyed with its own context current |

The single context is the biggest structural change. Rendering a second view
never needed a second context, and duplicating the caches per view meant that a
scene with four vision sensors uploaded every mesh and every texture four times.

### Shaders and lighting

* GLSL 3.30 core, with the shared declarations in one file
  (`resources/shaders/common.glsl`, pulled in by a small `#include` resolver).
* Camera data and all lights travel in **two uniform blocks** (`std140`) instead
  of ~60 individual uniforms whose names were rebuilt with `QString::append`
  and looked up in a hash map *per light, per frame*.
* **One `sampler2DArrayShadow`** holds every shadow map, instead of five
  `sampler2D` plus five `samplerCube` uniforms and a fully unrolled
  `dirLight0..4 / pointLight0..4 / spotLight0..4` block. Point lights use six
  layers with manual cube-face selection, so the "five lights" limit is gone
  (`kMaxLights = 32`, bounded by the shadow layer budget).
* Hardware depth comparison (`GL_COMPARE_REF_TO_TEXTURE`) gives a free 2x2 PCF
  footprint per tap, with a configurable 3x3 kernel on top.
* Instanced rendering: one draw call per (geometry, material state) instead of
  one per object. This is where the "3 draw calls for 7 objects" in the smoke
  test comes from.
* Per-instance normal matrices are computed on the CPU. The legacy shader did
  `mat3(transpose(inverse(model)))` **per vertex**, which is both slow and wrong
  for the skewed poses CoppeliaSim produces when a shape is scaled.
* Shading happens in **linear space**; textures are uploaded as
  `SRGB8_ALPHA8` so the sampler decodes them for free, and the tone-map pass
  applies exposure, an optional tone curve and the sRGB transfer function.
* Translucent geometry is sorted back to front, drawn with depth writes off and
  alpha blending on. The legacy renderer blended everything in submission order
  with the depth buffer still being written.
* Shapes honour CoppeliaSim's per-shape back-face culling flag (`data[23]`) in
  every pass (shaded, colour coded, depth prepass and shadow maps). Markers
  always cull their back faces, whatever the settings say, and are blended with
  their colour's alpha channel (the pass enables `GL_SRC_ALPHA,
  GL_ONE_MINUS_SRC_ALPHA`, except in colour coded frames where the handle bytes
  must survive untouched).
* Environment fog is supported (linear / exponential / exponential squared).

### Vertex layout — the one thing to get right

CoppeliaSim does **not** send a uniform per-vertex layout in
`extrenderer_mesh`:

| array | `data[]` | size |
|---|---|---|
| positions | `0`, `1` | one per unique vertex, addressed through `indices` |
| indices | `2`, `3` | 3 per triangle |
| normals | `4`, `5` | one per **triangle corner**, in index order |
| texture coordinates | `9`, `10` | one per **triangle corner**, in index order |

The engine sizes the normals from `_indices.size()` in
`CMesh::pushGenesisOrChangeEvent()`, and `CTextureProperty::getTextureCoordinates()`
only accepts a coordinate array whose length equals `triangles.size()`.

So for corner `c`: `position = positions[3 * indices[c]]`, but
`normal = normals[3 * c]` and `uv = texCoords[2 * c]`. Because every corner
carries its own normal, the renderer expands the geometry to one vertex per
corner and draws it non-indexed — an index buffer could not deduplicate
anything, and keeping one would only waste memory.

Both counts are passed to `SceneRenderer::uploadGeometry()`, which compares
them with the corner count and falls back to per-vertex indexing for an array
that really is per vertex.

### Correctness fixes inherited from the legacy plugin

These are real bugs in `../sourceCode`, and the new renderer does not reproduce
them:

1. `COpenglOffscreen::isResolutionSame()` used `resY = _resY` (assignment), so
   it always returned true and a vision sensor whose resolution changed kept
   rendering into the old framebuffer.
2. `Mesh::blankTexture` was a file-scope global shared by all meshes: the last
   mesh created owned it and every destructor deleted it.
3. The fallback textures were generated with `glGenTextures` but never given
   storage, so sampling them was undefined.
4. `Light` allocated a depth FBO and texture in its constructor and
   `lightContainer->removeAll()` ran at the end of *every* frame, so all shadow
   maps were recreated every frame.
5. `#ifdef _WIN32 static PFNGLACTIVETEXTUREPROC glActiveTexture =
   wglGetProcAddress(...)` — a raw Win32 entry point lookup next to an otherwise
   Qt-based renderer.
6. The spot light shadow probe used the cone's half angle as the *full* vertical
   field of view, clipping the shadow before the edge of the light cone.
7. `mesh->store(..., float* colors, ...)` kept a pointer into a buffer owned by
   CoppeliaSim and dereferenced it in a later callback.

### Kept on purpose

* **Markers** are supported for every type the callback still sends (points,
  lines, triangles, spheres, squares, discs, cubes, cylinders and `custom`
  meshes), with the `overlay` (drawn without depth testing) and `notshaded`
  (plain colour, unlit) options; the marker frame is always applied. The
  engine sends primitive data rather than geometry, so the marker pass
  tessellates it on the CPU into points, lines and triangles - unit shapes for
  the primitives, and the indexed mesh for `custom`. Markers are
  shaded diffuse-only by the scene lights (no shadows, no specular, like
  CoppeliaSim's own marker rendering) and drawn after the scene, into the
  multisampled target, so overlay markers land on top of the resolved image.
  Markers are shaded by default; `sim_markeropts_notshaded` draws an item with
  its plain colour instead, with no light and no ambient contribution. The
  marker frame from
  `data[13]`/`data[14]` is always applied (the `local` option no longer exists):
  points and per-item quaternions are relative to it. It arrives in double
  precision, with the quaternion in (qx, qy, qz, w) order.
* **Colour coded frames** are supported: when the start callback asks for one
  (`data[29]` != 0, a vision sensor in "color coded" render mode) the scene is
  drawn through a flat, unlit program that writes the object handle as the
  colour, `(h & 255, (h >> 8) & 255, (h >> 16) & 255)`, with no shadows, no
  exposure/tone mapping and no sRGB encode, so the read-back bytes decode back
  to the handles exactly. Meshes use `data[32]`, markers `data[15]`; the
  background is white.
* Blinn-Phong with CoppeliaSim's diffuse/specular material model, so scenes
  keep their look: the diffuse term is material diffuse x light diffuse, the
  highlight is material specular x light specular. CoppeliaSim shapes only
  carry diffuse + specular; a full PBR
  model would have nothing to feed it.
* The same per-light tuning keys read through `simGetExtensionString`:
  `nearPlane`, `farPlane`, `orthoSize`, `shadowTextureSize`, `bias`,
  `normalBias` (`@lightProjection@openGL3`), plus `noShadow`. Scenes that were
  tuned for the legacy renderer keep their settings.
* The same depth linearisation formula that `CVisionSensor` applies to its own
  `glReadPixels` result, but evaluated per fragment in the shaders: the depth
  buffer already holds linear values, so no per pixel pass is needed after the
  read-back. Values agree to float precision.

---

## Point clouds and oc-trees

Neither object type is part of the external renderer callback stream, so the
plugin walks the scene through simLib-2 right after the shapes have been
submitted:

```
simGetObjects(index, sim_sceneobject_pointcloud)   index from 0, < 0 ends the walk
simGetObjects(index, sim_sceneobject_octree)
simGetBoolProperty (handle, "visible", ...)         skip the object when false
simGetBufferProperty(handle, "packedPoints", ...)   float triples
simGetBufferProperty(handle, "colors", ...)         rgba bytes, optional
simGetIntProperty   (handle, "pointSize", ...)      point clouds: pixels
simGetFloatProperty (handle, "voxelSize", ...)      oc-trees: edge length
simGetBoolProperty  (handle, "randomColors", ...)   hash colours instead of the buffer
simGetObjectPose    (handle, -1, pose[7])           x, y, z, qx, qy, qz, qw
```

Both are drawn in the object's own frame, unlit, right after the shapes and
before the markers:

* point clouds as `GL_POINTS` with `gl_PointSize` from `pointSize` (one draw per
  cloud, since the size is a uniform), unlit like the engine draws them,
* oc-trees as instanced unit cubes of `voxelSize`, lit with the scene lights and
  ambient (one draw for all of them; centre, size, colour and the lit flag vary
  per instance).

Both write the same **linear depth** the shaded pass writes (`gl_FragDepth` in
their fragment shaders) and are depth tested against the scene. That second part
is not optional: the depth buffer holds linearised values, so a shader that
writes plain window-space depth compares ~0.99 against everything else and the
whole object hides behind the scene.

Buffers are copied out of the engine's memory immediately: `simGetBufferProperty`
hands back a buffer owned by the caller, so it is `simReleaseBuffer`ed as soon as
it has been copied. Colours are treated as authored sRGB, like every other colour
in the plugin; `randomColors` uses a hash of the point index, so points keep their
colour from frame to frame instead of flickering.

In a colour coded frame both are drawn with their object handle as the colour,
exactly like meshes and markers.
## No Qt, and no window system requirement

The plugin has no Qt dependency at all. `dumpbin /dependents` on the built DLL
lists `OPENGL32`, `GDI32`, `USER32` and the C runtime — no `Qt5Core`,
no `Qt5Gui`. Concretely:

* GL entry points come from a 77-entry table (`glApi.h`) loaded through the
  platform loader, not from `QOpenGLExtraFunctions`;
* the maths is a 400 line `math3d.h` validated operation by operation against
  Qt (`tools/mathParity`), including the column-major layout the uniform
  uploads depend on;
* shaders are embedded C++ string literals generated from `resources/shaders/*`
  by `tools/embed_shaders.cmake`, so there is no `rcc` step and no resource
  lookup at run time;
* settings come from `simOpenGL3.ini` in CoppeliaSim's resources directory,
  located through `simGetStringProperty(sim_handle_app, "paths.resources", &state)`
  rather than guessed from the module path, with the defaults compiled in as the
  fallback;
* the off-screen context is created directly on the platform:
  WGL on Windows, EGL on Linux, CGL on macOS (see
  `offscreenContextWin32.cpp` / `offscreenContextEgl.cpp` /
  `offscreenContextCgl.cpp` behind `offscreenContext.h`).

That last point is what makes **headless** mode possible: a headless
CoppeliaSim runs on a `QCoreApplication` (see `sourceCode/various/simQApp.h`),
and `QOffscreenSurface` refuses to work without a `QGuiApplication`. On Linux
the EGL backend uses `EGL_MESA_platform_surfaceless`, so no X server or Wayland
compositor is needed either.
## Requirements

* CoppeliaSim 4.5.1.7 or newer (built and verified against the 4.10 and 4.11
  header trees).
* A C++17 compiler. No Qt headers or libraries are needed to build the plugin.
  (`qmake` is still a convenient makefile generator, and comes from a Qt
  install, but it is a build-time tool only; the CMake path needs no Qt at all.)
* OpenGL 3.3 core. Verified with an NVIDIA driver; the code only uses core 3.3
  features (VAOs, uniform blocks, texture arrays, instancing, MSAA
  framebuffers, `sampler2DArrayShadow`).
* A desktop session on Windows (WGL needs a device context; the window is
  hidden and never shown). Linux works with or without a display server.

## Build

qmake, matching the layout of the legacy project:

```
cd programming/simOpenGL3
"C:\Qt\5.15.0\msvc2019_64\bin\qmake.exe" simOpenGL3.pro -spec win32-msvc
nmake -f Makefile.Release
```

The result is `release/simOpenGL3.dll`; there is no debug/release split in the
output name. Copy it next to `coppeliaSim.exe`. `jom -f Makefile.Release` works
as well.

The platform backend is selected by the build file: `offscreenContextWin32.cpp`
on Windows, `offscreenContextEgl.cpp` on Linux (links `-lEGL`), and
`offscreenContextCgl.cpp` on macOS (links `-framework OpenGL`).

CMake:

```
cmake -S . -B build -DCOPPELIASIM_INCLUDE_DIR=../include
cmake --build build --config Release
```

`COPPELIASIM_INCLUDE_DIR` defaults to `../include` (i.e.
`programming/include` of the checkout) or `$COPPELIASIM_ROOT_DIR/programming/include`.

The Lua convenience file is `lua/simOpenGL3.lua`, installed like `../lua`.

## Developer smoke test

Shader errors are painful to diagnose from inside CoppeliaSim, so the renderer
ships a small harness that runs the real code path without the host
application:

```
cd tools/smokeTest
qmake smokeTest.pro && nmake -f Makefile.Release
release\smokeTest.exe
```

It needs no Qt and no window: it creates its context through the same platform
backend the plugin uses, so it also runs on a headless build machine (Linux: no
X server required).

It renders a synthetic scene twice (perspective + MSAA + shadows, then
orthographic + no MSAA + depth prepass + ACES), writes `smoke.png` and
`smokeOrtho.png`, and fails if the image is empty, the depth buffer is empty or
any GL error was raised. It is a development tool, not part of the plugin.

The test geometry deliberately mirrors the engine's layout — 8 shared positions
with 36 per-corner normals — so a wrong normal basis shows up immediately as
mis-shaded faces instead of passing silently. (Verified by temporarily forcing
per-vertex indexing: every cube face then shades wrongly.)

![perspective, 4x MSAA, one directional and one point light](tools/smokeTest/smoke.png)

![orthographic, no MSAA, depth prepass, ACES tone mapping](tools/smokeTest/smokeOrtho.png)

`tools/mathParity` is the other development tool: it compares `math3d.h` with
Qt's `QMatrix4x4`/`QVector3D` for a few hundred random transforms (lookAt,
rotate, translate, scale, perspective, ortho, products, inverse, normal matrix,
vertex mapping) and was used to prove the maths port is exact. It needs Qt and
is therefore off by default (`SIMOPENGL3_BUILD_MATH_PARITY`).

## Configuration

Settings are the defaults in `sourceCode/renderSettings.h`, optionally overridden
by `simOpenGL3.ini` in **CoppeliaSim's resources directory**. The directory is
queried at load time, not guessed:

```cpp
char* location = nullptr;
const int result = simGetStringProperty(sim_handle_app, "paths.resources", &location);
// <location>/simOpenGL3.ini
if (result == sim_propertyret_ok && location != nullptr)
    simReleaseBuffer(location);
```

The file is read once, when the plugin is loaded, so changes need a restart.
If the directory cannot be queried or the file is absent, the built-in defaults
are used — and a warning says so, because "I edited the file and nothing
changed" is almost always a file that is not where the plugin looks. Invalid
values and unknown keys are reported the same way, naming the file, the key and
the offending value.

```ini
[antiAliasing]
samples=4            ; 1 disables MSAA, otherwise clamped to GL_MAX_SAMPLES

[shadows]
enabled=true
mapSize=1024         ; per light, square, in texels
maxLayers=64         ; directional/spot = 1 layer each, point light = 6
polygonOffset=true   ; slope-scaled bias in the depth pass
pcfRadius=1          ; spacing of the 3x3 PCF taps, in texels

[shading]
gammaCorrect=true    ; decode surfaces from sRGB, light in linear space
toneMapping=none     ; none, reinhard or aces
exposure=1.0
fog=true

[geometry]
cullBackFaces=true
depthPrepass=false
frustumCulling=true
```

`gammaCorrect` decodes surface colours and textures from sRGB, lights in linear
space and encodes the finished image, which is the only correct way to get
texture filtering and blending right. Light colours and the scene ambient are
used as authored — they are intensity multipliers rather than display colours,
and CoppeliaSim's own renderer used them verbatim; decoding them as well would
darken every light by roughly a factor of two. Turning the option off bypasses
the whole conversion.

Shadow memory: the atlas is one `DEPTH_COMPONENT24` texture array of
`maxLayers x mapSize x mapSize`. At the defaults, a directional light costs one
1024x1024 layer (~3 MB) and a point light six (~18 MB).

## Pipeline

```
extrenderer_start ─┐
extrenderer_light ─┤  FrameData { camera, lights[], items[] }
extrenderer_mesh  ─┘
        │
        ▼
  shadow passes      1 layer per directional/spot light, 6 per point light,
        │            batched by geometry, polygon offset + normal bias
        ▼
  depth prepass      optional, camera depth only, same instanced batches
        │
        ▼
  forward pass       opaque batches -> translucent per-object, back to front
        │            into the RGBA16F multisampled target
        ▼
  resolve            glBlitFramebuffer of colour + depth into texture targets
        │
        ▼
  tone map           exposure -> curve -> sRGB, into the RGBA8 target
        │
        ▼
  readback           glReadPixels through a PBO; colour and depth share one
        │            synchronisation (see Performance)
        ▼
extrenderer_stop
```

## Performance

The read-back is what a frame costs, not the raster work: the plugin has to hand
the image to CoppeliaSim before the callback returns, so every frame ends with a
synchronous `glReadPixels` + map. Measured with the built-in benchmark
(`smokeTest.exe`, 7 draw calls, 21 shadow draws, 56 instances):

| 1280x720 | before | after |
| --- | --- | --- |
| full frame (colour + depth read) | 1.90 ms | 1.34 ms |

Where the remaining 1.34 ms goes:

| stage | cost | note |
| --- | --- | --- |
| GPU: shadows, forward pass, MSAA resolve, tone map | 0.41 ms | shadow map cache would remove most of this on static scenes |
| depth read-back | 0.64 ms | 3.7 MB, 4 bytes per pixel |
| colour read-back | 0.45 ms | 2.8 MB, RGB8 |
| CPU work after the read | ~0 ms | was 0.35 ms when depth was linearised on the CPU |

Two changes got it there:

* **Depth is linearised on the GPU** (`gl_FragDepth` in `scene.frag`,
  `marker.frag`, `colorcode.frag` and `depth.frag`), so the read-back is a plain
  transfer and no pass over the image follows. The depth prepass and the shadow
  layers share `depth.frag`, which is why the clip planes are cleared for the
  shadow passes: they must write plain window-space depth.
* **One synchronisation per frame instead of two**: colour and depth are queued
  into two pixel-pack buffers and fetched afterwards (`PixelReader::queue()` /
  `fetch()`), so the GPU is waited for once rather than twice.
* **The camera and light uniform buffers are re-bound every frame.** Binding
  points are global GL state; the engine's own renderer (or a second renderer
  instance) can bind something else to them, and then this renderer silently
  reads a foreign camera matrix - geometry appears at a random place that moves
  with the camera.

Points worth knowing before optimising further:

* MSAA costs nothing measurable here (0.02 ms): the resolve is bandwidth, not
  fill rate.
* Colour read-back is pure transfer (2.8 MB at 1280x720); the only way to cut it
  is to not need the image.
* `gl_FragDepth` disables early-Z. That cost more than the CPU conversion saved
  on a per-fragment heavy scene, so if a user reports a regression there, the
  alternative is to keep window depth and linearise on the GPU into an extra
  R32F attachment instead - slightly more code, keeps early-Z.
* A static scene renders the same shadow maps every frame; caching them behind a
  signature of lights, item transforms and a geometry version counter is the
  next large win (about 0.3 ms of the 0.41 ms GPU time here, more in real
  scenes).
## File map

| file | contents |
|---|---|
| `sourceCode/simOpenGL3.cpp` | plugin entry points, message dispatch, per-view target cache, light tuning |
| `sourceCode/sceneRenderer.cpp` | passes, batching, caches, per-corner vertex expansion, uniform blocks, depth linearisation, marker tessellation |
| `sourceCode/offscreenTarget.cpp` | multisample / resolve / present framebuffer chain, readback |
| `sourceCode/shadowAtlas.cpp` | depth texture array atlas and layer attachment |
| `sourceCode/offscreenContextCommon.cpp` | GL table loading, version/renderer reporting |
| `sourceCode/offscreenContextWin32.cpp` | WGL context: hidden window, bootstrap context, 3.3 core |
| `sourceCode/offscreenContextEgl.cpp` | EGL context, surfaceless when available (no X server) |
| `sourceCode/offscreenContextCgl.cpp` | CGL context (macOS) |
| `sourceCode/glApi.cpp` | GL 3.3 entry point table with WGL/GLX/EGL/dlsym fallbacks |
| `sourceCode/glUtil.cpp` | RAII GL objects, shader compilation, PBO reader |
| `sourceCode/math3d.h` | `Vec2/3/4`, `Mat3/4` with Qt-compatible conventions |
| `sourceCode/log.cpp` | warning/error sink, so the renderer can run outside CoppeliaSim |
| `sourceCode/sceneTypes.h` | std140 layout mirror, `DrawItem`, `LightShadowInfo` |
| `sourceCode/renderSettings.cpp` | INI parsing and the defaults |
| `simOpenGL3.ini` | documented default configuration, to copy into the resources directory |
| `sourceCode/shaders_embedded.h` | generated from `resources/shaders/*` by `tools/embed_shaders.cmake` |
| `resources/shaders/` | GLSL: forward pass, depth passes, full screen tone map |

## Troubleshooting

The plugin only logs warnings and errors, through `simAddLog`, so a healthy run
is silent apart from CoppeliaSim's own `plugin simOpenGL3: loading... / load
succeeded`. Anything the plugin prints names a concrete failure:

| message | meaning |
|---|---|
| `missing OpenGL entry point <name>` | the GL table could not be resolved for this context |
| `cannot create the off-screen OpenGL context` | platform context creation failed (WGL/EGL/CGL); the renderer is off |
| `the context reports "..." ... continuing` | the version string looked older than 3.3; shader compilation decides |
| `shader ... failed to compile/link` | with the GLSL log, the actual rejection |
| `<name> framebuffer is incomplete` | the render target could not be created |
| `cannot create a WxH anti-aliased render target, retrying without MSAA` | fallback path, not fatal |
| `the depth buffer could not be resolved ...` | colour is fine, depth reads are wrong |
| `shader program N has no active uniform block <name>` | the shaders would read zero matrices/lights |
| `the OpenGL context was created by another thread` | the protocol guarantees this cannot happen; report it |
| `the shadow atlas is capped at ...` | more shadow-casting lights than layers |
| `no configuration file at "..."` | `simOpenGL3.ini` is not in the resources directory, so the defaults are in effect |
| `<file>: value "..." for "..." is not valid` | an INI value could not be parsed; the default stays |
| `<file>: unknown setting "..."` | an INI key this build does not know (typo, or newer file) |
| `cannot query the resources directory` | `paths.resources` was unavailable, so the INI was not read |
| `simGetStringProperty is not available in this build` | the plugin ABI did not expose the property API; the INI was not read |

Two behaviours worth knowing, because they are what makes the plugin coexist
with the host renderer:

* `makeCurrent()` remembers which GL context was current and `doneCurrent()`
  restores it. CoppeliaSim (and Qt's paint engine) keep their own context
  bound to the same thread while the external renderer runs, so clearing the
  binding instead of restoring it would pull the context out from under them.
* The context is created on the thread that issues the first `extrenderer_start`
  and must stay on it; using it from another thread is unsupported and is
  reported.

`tools/smokeTest` is the fastest way to tell a renderer bug from a host
interaction bug: it drives the same renderer and the same platform backend but
without CoppeliaSim, so if it passes there and CopperliaSim crashes, the problem
is in the glue rather than in the GL code.
## Known limitations

* **Edges are not drawn.** CoppeliaSim sends a per-vertex edge bitmap
  (`data[26]`/`data[27]`) that the built-in renderer turns into a wireframe
  overlay. The legacy plugin ignored it as well; doing it properly means
  building a second index buffer per mesh.
* **Translucent objects cast fully opaque shadows**, because they are rendered
  into the shadow maps like everything else.
* **Fog is ignored for vision sensors.** CoppeliaSim passes `data[14..16]` as
  `double*` for vision sensors but `float*` for cameras; the renderer validates
  the values and falls back to no fog rather than fogging the whole image.
* **The shadow atlas size is global.** A per-light `shadowTextureSize`
  extension string larger than the configured `mapSize` raises the atlas size
  for *all* lights, because all layers of a texture array share one size.
* **No GPU side sorting or occlusion culling.** Translucent sorting is a CPU
  `std::sort` over the frame's items, which is fine for the object counts
  CoppeliaSim scenes typically have.
* **Single threaded.** The renderer assumes it is called from one thread (the
  thread CoppeliaSim renders on), which is what the external renderer protocol
  guarantees.
