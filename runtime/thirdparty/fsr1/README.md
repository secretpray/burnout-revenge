# AMD FidelityFX Super Resolution 1 (vendored)

`ffx_a.h` and `ffx_fsr1.h` from https://github.com/GPUOpen-Effects/FidelityFX-FSR at commit
a21ffb8f6c13233ba336352bdff293894c706575 (2021-11-15; its `ffx-fsr/` folder), unmodified, MIT
license (`LICENSE.txt`, the repository's `license.txt` as it is; also
`LICENSES/FidelityFX-FSR-MIT.txt`).

SHA-256:

    f60e2722fcd13989523b9164d776ab382b3692791767f3bf8bb19967f763f3fb  ffx_a.h
    93c3922362ea7fc99cbcc698ca30c98de4f8c246d1fbb0b09e015ddef38ce3a5  ffx_fsr1.h
    db089274ce766da70f5b7d791029c3486f9f9e27c8c79c652689603d3192e802  LICENSE.txt

Used by Settings > Graphics > Upscaling's FSR 1 (`runtime/video/fsr1.cpp`): their 32-bit GLSL
EASU and RCAS functions are compiled into the presenter's shaders (the build embeds both files'
text, `generated/fsr1_headers.inc`), and their CPU code computes the shaders' constants.
