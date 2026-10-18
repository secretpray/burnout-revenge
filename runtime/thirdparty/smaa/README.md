# SMAA (vendored)

SMAA, Enhanced Subpixel Morphological Antialiasing, by Jorge Jimenez, Jose I.
Echevarria, Belen Masia, Fernando Navarro and Diego Gutierrez
(https://github.com/iryoku/smaa, commit 71c806a838bdd7d517df19192a20f0c61b3ca29d),
MIT license (`LICENSES/SMAA-MIT.txt`). Unmodified:

- `SMAA.hlsl`: the shader code, compiled for Settings > Graphics > Anti-Aliasing's
  SMAA option (video/smaa.cpp) through its `SMAA_CUSTOM_SL` hooks;
- `Textures/AreaTex.h` and `Textures/SearchTex.h` as `AreaTex.h`, `SearchTex.h`:
  the precomputed lookup textures.
