// Renderer configuration: compile-time defaults, optionally overridden by an
// INI file that lives next to CoppeliaSim's own resources
// (<paths.resources>/simOpenGL3.ini).
//
// The plugin reads nothing else from disk, so a missing file simply keeps the
// defaults below.

#pragma once

#include <string>

namespace modern
{

enum class ToneMapping
{
    None,
    Reinhard,
    Aces
};

struct RenderSettings
{
    // --- anti-aliasing -----------------------------------------------------
    int msaaSamples = 4; // clamped to [1, GL_MAX_SAMPLES]; 1 disables MSAA

    // --- shadows -----------------------------------------------------------
    int shadowMapSize = 1024; // per light, square
    int maxShadowLayers = 64; // hard cap of the shadow texture array
    bool shadowsEnabled = true;
    bool polygonOffsetShadows = true; // cheap slope-scaled bias in the depth pass
    int pcfKernelRadius = 1;          // spacing of the 3x3 PCF taps, in texels

    // --- shading -----------------------------------------------------------
    bool gammaCorrect = true; // treat incoming colours/textures as sRGB
    ToneMapping toneMapping = ToneMapping::None;
    float exposure = 1.0f;

    // --- geometry ----------------------------------------------------------
    bool cullBackFaces = true; // honour CoppeliaSim's per-shape culling flag
    bool depthPrepass = false; // extra depth-only pass, helps heavy overdraw
    bool frustumCulling = true;

    // --- environment -------------------------------------------------------
    bool fogEnabled = true;


};

// Name of the configuration file inside the resources directory.
extern const char* const kConfigFileName;


// Reads <directory>/simOpenGL3.ini on top of the defaults and returns the
// result.  A missing file, unreadable values and unknown keys are all reported
// as warnings: each one means the file the user thinks is in effect is not.
RenderSettings loadRenderSettings(const char* directory);

} // namespace modern
