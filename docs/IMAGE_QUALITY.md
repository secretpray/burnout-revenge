# Picture settings

Open **Settings > Graphics** to adjust the picture. Changes apply live.

## Resolution and scaling

Internal Resolution controls the game's rendering resolution, from 1x (720p)
to 3x (2160p). Auto chooses from the window size. A higher setting improves
fine detail and uses more GPU time and memory.

Upscaling enlarges the rendered picture to the output window:

- **Bicubic** is the default, with adjustable sharpening.
- **Bilinear** is a simple, softer filter.
- **FSR 1** uses AMD's EASU scaling and RCAS sharpening.
- **MetalFX Spatial** uses Apple's spatial scaler and falls back to Bicubic
  on unsupported devices. It is experimental.

These are spatial scalers. They do not generate additional game frames.

## Anti-aliasing and sharpening

**SMAA 1x** is the default. **FXAA** is also available. FXAA handles the
Hor+ and Vert+ picture bands when their geometry requires it. Turn
anti-aliasing off to retain the unfiltered picture.

Sharpening adjusts fine-detail contrast. Excessive sharpening can emphasize
edges and noise; choose it together with the internal resolution and scaler.
Anisotropic Filtering improves textures viewed at an angle, up to 16x.

## Bloom and motion blur

Both controls range from 0 to 100%. Lower Bloom reduces haze and edge glow
while preserving the game's sky brightness. Lower Motion Blur shortens the
streaks at speed; 0% removes them. The original bloom/luminance chain keeps
its own low-resolution targets as internal resolution increases.

## Vehicle detail

**Original**, **Far** and **Farthest** multiply the car and traffic model
switch distances by 1, 2 and 4. Far is the default. Higher settings retain
detailed models farther away and can increase rendering work. Track scenery
and the traffic population are unchanged.

For technical rendering details, see [RENDERING.md](RENDERING.md).
