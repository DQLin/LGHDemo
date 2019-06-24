# Real-Time Rendering with Lighting Grid Hierarchy I3D 2019 Demo

### Daqi Lin

This demo is for the I3D 2019 paper Real-Time Rendering with Lighting Grid Hierarchy (Lin & Yuksel, 2019). The project page
of the paper can be found at 
<https://dqlin.xyz/pubs/2019-i3d-LGH/>.

This demo is built upon [MiniEngine](https://github.com/microsoft/DirectX-Graphics-Samples/tree/master/MiniEngine) by Team Minigraph at Microsoft.  

## System Requirements
* Windows 10
* Visual Studio 2017 with Windows 10 SDK version >= 17763
* DXR compatible graphics card, RTX 2060 or higher is recommended
* For non-DXR compatible graphics cards, the application will try to enable the fallback layer, 
but the fallback layer is not guaranteed to work. See
the fallback layer requirements [here](https://github.com/microsoft/DirectX-Graphics-Samples/blob/master/Samples/Desktop/D3D12Raytracing/readme.md)
and the discussion in this [thread](https://github.com/microsoft/DirectX-Graphics-Samples/issues/475#issuecomment-434236284).


## Libraries Used
* ZLib
* WinPixEventRuntime
* Assimp 3.3
* FreeImage 3
* TinyXml
* GLM

If the prebuilt binaries do not work with your computer, you should easily find them online.

## Run the demo:
* To run the demo, enter LGHDemo/ and open LGHDemo.exe
* In command lines, run "LGHMemo.exe <XResolutionxYresolution>", for example, "LGHDemo.exe 1920x1080", to change rendering resolution
* In command lines, run "LGHDemo.exe <YourSceneDecsription>.xml" to view a custom model rather than the demo scene. A file "SceneDescription.xml" is 
provided as the template. Current application only supports one directional light and one perspective camera.

## Build the demo:
* open LGHDemo/LGHDemo.sln in Visual Studio 2017 or above (may need to upgrade the solution)
* Choose configuration: Debug, Profile or Release
* Build the solution (x64 platform is assumed)

## Controls
* forward/backward/strafe: WASD (FPS controls)
* up/down: E/Q
* yaw/pitch: mouse
* toggle slow movement: lshift
* open/close demo settings menu: backspace
* show/hide frame rate: =
* navigate debug menu: use up/down arrow keys to go through menu items, use left/right to fold/unfold submenus
* toggle debug menu item (boolean type, the toggled state looks like [X]): return
* adjust debug menu value (numeric or enum type): left/right arrow keys
* Navigate to "Save Settings" and press enter to save current settings to a file called "SavedSettings.txt"
* Navigate to "Load Settings" and press enter to load settings from "SavedSettings.txt", assuming it exists
* Navigate to "Display Profiler" and press enter to open the profiler menu. Use numpad 5123 as arrow keys to navigate
* Press ESC to exit the demo

## Demo Settings

#### Application
_All options related to techniques used in the paper._

Debug View
    : Iterate through 4 different debug views for shadow sampling - stochastic and filtered results of unshadowed and shadowed indirect lighting

Direct Lighting Only
    : Switch off all indirect illumination

Enable Indirect Shadow
    : Switch on shadow in indirect illumination

Enable Shadow TAA
    : Switch on temporal anti-aliasing for indirect illumination shadow, see section 4.3 last paragraph in the paper

Enable Temporal Random
    : Switch on chaging pseudo-random seed for shadow sampling in every frame. Deterministic bias for indirect shadow sampling can be 
eliminated when this is on. It is recommended to combine this with Shadow TAA and SVGF filtering.

Filter Type for Shadows
    : Iterate through 3 different shadow filters available: wavelet À-trous filter [Dammertz et al., 2010], 
Spatiotemporal Variance-Guided Filtering (SVGF) [Schied et al., 2017] and a naive bilateral filter [Heitz et al., 2018]. 

Filtering 
    : Options for shadow filtering

* Interleave
    : Parameters for generating the discontinuity buffer used for geometry-aware Gaussian blur in filtering interleaved sampling patterns
 * NDiff
    : Normal difference threshold. A lower threshold causes less tolerance in normal difference for determining geometry discontinuity
 * ZDiff
    : Depth difference threshold. A lower threshold causes less tolerance in depth difference for determining geometry discontinuity
    
* SVGF
    : Parameters for the SVGF filtering
 * CPhi
    : Lower value causes the bilateral filter more sensitive to color difference
 * NPhi
    : Lower value causes the bilateral filter more sensitive to normal difference
 * PPhi
    : Lower value causes the bilateral filter more sensitive to positional difference
    
* Wavelet
    : Parameters for the Wavelet À-trous filtering
 * CPhi
    : Lower value causes the bilateral filter more sensitive to color difference
 * NPhi
    : Lower value causes the bilateral filter more sensitive to normal difference
 * PPhi
    : Lower value causes the bilateral filter more sensitive to positional difference
 * Strength
    : Chooses from 3 different levels of filter strength. 0 - use the unfiltered input as the guidance image
    for detecting color difference, 1 - an interpolation between between 0 and 2
    2 - always use the filtered output as the guidance image for the next iteration
 
LGH
   : Sets parameters for lighting grid hierarchy construction, lighting computation and shadow sampling

* Alpha
   : The blending parameter for blending different LGH levels. Larger value improves the accuracy of the result
   
* Build Source
   : Choose between "From S1" (Gather from S1) and "From VPLs" (Scatter VPLs). See section 3.1 in the paper. 
   WARNING: Choosing "From VPLs" might cause your GPU to reset due to timeout, since this method is extremely slow.
    
* DevScale
   : Adjust the scaling factor for the standard deviation of LGH shadow sampling. Using a smaller DevScale increases
   bias in shadow, but reduces the variance
   
* Draw Levels
   : Choose between "Skip VPLs" and "Include VPLs". See section 4.1 in the paper. Choose "include VPLs" slightly increases
   the frame time but improves the result accuracy
   
* Interleave Rate
   : Choose the interleave sampling rate, N/A (no interleaved sampling), 2x2 and 4x4. 

* shadow levels
   : Choose between "From S2" and "From minLevel". "minLevel" is 0 if Draw Levels is set to "Include VPLs", other wise it is 1.
   See section 4.1 in the paper. Choose "From minLevel" slightly increases the frame time but improves the result accuracy
   
* Shadow rate
   : Choose the shadow sampling rate. 1 - 1 shadow samples per pixel, 2 - 4 shadow samples per pixel.
   
Lighting
   : Sets parameters for direct lighting (sun light) You can tune these paremeters if you find sun shadow for your custom model looks incorrect

* Sun Inclination
   : Controls the sun elevation angle, a value from 0.0 to 1.0. 1.0 corresponds to vertical sunlight and 0.0 corresponds to horizontal sunlight
   
* Sun Light Intensity
   : Sun light intensity. Decrease this if you find the scene too bright. Alternatively, you can change the exposure to decrease the visual brightness in
   in Graphics>HDR>Exposure.
   
* Sun Orientation
   : Radian of the azimuthal sun light angle.

* Sun Shadow Center [X|Y|Z]
   : Sets the center location on far bounding plane of the shadowed region

* Sun Shadow Dim [X|Y|Z]
   : Sets the bounds for the orthographic projection for the directional light shadow map
      
VPL
   : Sets parameters that controls VPL generation

* Density
   : VPL density, a value from 0.1-40.0. 0.1 corresponds to 64 VPLs, and 40.0 corresponds to about 10.5 million VPLs in the demo scene.
   Default is 3.9, which generates about 100000 VPLs.   
   Depending on your scene and ray depth, more than 11 million VPLs generated might cause the program to crash.
   
* Max Ray Depth
   : Light ray depth for random walk generation of VPLs (default: 3), a value from 1 to 10. Notice that this demo only considers 
   diffuse bounces.
   
* Preset Density Level
   : 5 preset density levels are available. These density levels corresponds to about 1k, 10k, 100k, 1M, and 10M VPLs generated in the demo scene.

#### Graphics
_The original MiniEngine post effect settings. Including toggling FXAA and TAA, Bloom filter, depth of field, HDR, motion blur._

#### Timing

_VSync and frame rate limiting options._


## Additional notes

* uncomment USELOCK macro in LigthingComputationPS.hlsl and to enforce using thread locks for shadow sampling
* uncomment ENABLE_TEAPOT in LGHDemo.h and recompile the project to add a movable teapot in the demo scene, the teapot moving control keys can 
    be found starting from line 291 in LGHDemo.cpp
* uncomment GENERATE\_IR\_GROUND_TRUTH in LGHDemo.h and in ScreenShaderPS.hlsl, recompile to generate instant radiosity ground truth using 1M VPLs


For questions, please email daqi@cs.utah.edu or post an issue here.