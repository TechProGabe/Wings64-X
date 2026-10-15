# Renderer

## Textures

`xbox/src/gfx/texconv.c` turns TMEM-layout N64 texture data into the linear
`0xAARRGGBB` rows that `xgfx_tex_create` takes. It is pure C (no platform calls)
and is tested on the host by `tools/xbox/test_texconv.py`. It descends from
wings64-dc's `gfx_texconv.c`.

- **Formats**: RGBA16 (5551), RGBA32, IA4, IA8, IA16, I4, I8. I formats put the
  intensity in RGB and alpha, as the RDP does. Channel expansion is exact:
  5 bits `(v<<3)|(v>>2)`, 4 bits `(v<<4)|v`, IA4's 3-bit intensity
  `(v<<5)|(v<<2)|(v>>1)`, 1-bit alpha 0 or 255. CI and YUV return failure
  (PW64 uses neither; a TLUT would be a new argument).
- **Deswizzle**: TMEM stores odd rows with the two 32-bit halves of every 8-byte
  group swapped. `texconv_deswizzle` undoes that in place (self-inverse).
  `texconv_deswizzle_dxt` applies the exact LOADBLOCK rule for any `dxt`: RAM word
  `i` is swapped when `((i*dxt)>>11)&1` differs from the row parity, so a `dxt`
  that matches the line is a no-op and `dxt` 0 equals `texconv_deswizzle`.
- **Sizes and padding**: `texconv_line_bytes` gives the TMEM row stride;
  `texconv_pow2` rounds up to a power of two in [1, 1024] (the NV2A takes
  1-texel sides, unlike the Dreamcast's minimum of 8). The converted w x h image
  is padded into the dst_w x dst_h buffer by replicating the last column and last
  row, so clamp-mode sampling at the edge is right. The interpreter scales
  s,t by w/dst_w and h/dst_h.
- **Not here**: the texture cache, keying, eviction and the upload are the
  interpreter's and the back end's (`xgfx_tex_create`). Conversion is per-row,
  one function per format, with no per-texel divide.
