# P5D.7 — Color Pipeline Authority

## Why this milestone exists

Real Clean Screen validation after P5D.6 showed that thin gray borders could be
preserved while larger near-white gray surfaces still collapsed toward white,
and dark neutral hierarchy could compress toward black.

The production MP4 at that point reported no authoritative color range /
matrix / transfer metadata. Raw decoded luma also behaved like full-range Y
while downstream H.264 semantics could be interpreted as studio-range.

That is a pipeline contract bug, not a grading problem.

P5D.7 fixes color authority before any further Clean Screen tone-model work.

## Canonical SDR contract

Arssyut's default SDR screen-recording path is now:

```text
WGC / compositor
BGRA SDR desktop
full-range RGB
BT.709/sRGB primaries
        |
        v
D3D11 Video Processor
explicit color-space conversion
        |
        v
NV12
BT.709 YCbCr
studio range 16-235
        |
        v
Media Foundation H.264
High Profile
BT.709 primaries/matrix/transfer
studio range 16-235
```

There is exactly one intentional range conversion.

## D3D11 Video Processor authority

Preferred Windows 10 path:

- input: `DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709`;
- output: `DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709`;
- set through `ID3D11VideoContext1`.

Compatibility fallback:

- use the legacy `D3D11_VIDEO_PROCESSOR_COLOR_SPACE` API explicitly;
- RGB input range = full 0-255;
- YCbCr matrix = BT.709;
- output nominal range = 16-235;
- xvYCC disabled.

The fallback is still explicit. Driver defaults are not considered color
authority.

## Media Foundation authority

Both the NV12 encoder input media type and H.264 output media type explicitly
carry:

- `MF_MT_VIDEO_PRIMARIES = MFVideoPrimaries_BT709`;
- `MF_MT_TRANSFER_FUNCTION = MFVideoTransFunc_709`;
- `MF_MT_YUV_MATRIX = MFVideoTransferMatrix_BT709`;
- `MF_MT_VIDEO_NOMINAL_RANGE = MFNominalRange_16_235`.

The H.264 encoder documentation explicitly treats unknown nominal range as
16-235, but P5D.7 does not rely on that implicit rule. Range is always written
as an explicit attribute.

## Diagnostics

Recordings now expose:

- `encoder_color_pipeline`;
- `encoder_color_pipeline_authoritative`;
- `encoder_rgb_input_range = full_0_255`;
- `encoder_yuv_output_range = studio_16_235`;
- `encoder_color_primaries = bt709`;
- `encoder_transfer_function = bt709`;
- `encoder_yuv_matrix = bt709`.

Expected authoritative values are not inferred from the player.

## End-to-end gray-ladder gate

P5D.7 upgrades the Windows Media Foundation integration test from a single
solid-color frame to a neutral gray ladder containing:

```text
0, 8, 16, 24, 32, 64, 128, 192, 220, 232, 246, 255
```

The chart is passed through the real production path:

```text
BGRA texture
  -> D3D11 Video Processor
  -> NV12 studio-range
  -> H.264 MP4
  -> Media Foundation H.264 decoder
  -> NV12 readback
```

The test checks:

- H.264 native media type reports 16-235 nominal range;
- BT.709 primaries are present;
- BT.709 transfer function is present;
- BT.709 YCbCr matrix is present;
- decoded Y values approximately follow studio-range mapping;
- the gray ladder remains monotonic;
- near-white gray remains distinct from white;
- near-black gray remains distinct from black;
- decoded black/white endpoints remain in the studio-range neighborhood.

This is the regression gate for the exact failure class seen in real UI
recordings.

## Relationship to ArVisual

P5D.7 deliberately does **not** retune Clean Screen or Smart Auto.

A wrong/ambiguous color pipeline can make a correct grade look wrong. Any
screen-native tone-model work must therefore happen only after this milestone
is validated in a real recording.

Once P5D.7 is accepted, the next visual-engine milestone may safely address:

- neutral surface luma anchoring;
- gray-ladder preservation inside Clean Screen;
- screen-native Smart Auto semantics;
- UI-vs-media region behavior.

Do not compensate for color-range mistakes by weakening or strengthening
ArVisual parameters.

## Compatibility contract

The production format remains H.264 High/Main fallback + NV12 4:2:0.

P5D.7 changes color authority, not container/codec compatibility.

If `ID3D11VideoContext1` is unavailable, use the explicit legacy D3D11 color
space API. Do not silently revert to unknown/default range or matrix.
