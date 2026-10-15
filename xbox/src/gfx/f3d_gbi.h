/*
 * f3d_gbi.h - Fast3D (plain F3D, non-F3DEX) GBI constants and display-list
 * field decoders for the Fast3D -> PowerVR interpreter.
 *
 * Source of truth: external/pw64-decomp/include/libultra/PR/gbi.h with
 * F3DEX_GBI, F3DEX_GBI_2 and F3DLP_GBI all UNDEFINED (the #else branches).
 * Every #define carries "gbi.h:N", the line it came from. Immediate opcodes
 * are written as the byte that lands in w0 bits 24..31: G_IMMFIRST is -65,
 * so G_IMMFIRST-n is 0xBF-n.
 *
 * Names use an F3D_ prefix so this header can sit in the same translation
 * unit as the decomp's gbi.h. Only <stdint.h> is included.
 */
#ifndef WINGS64_GFX_F3D_GBI_H
#define WINGS64_GFX_F3D_GBI_H

#include <stdint.h>

/* ---- RSP immediate opcodes (gbi.h:138-157, G_IMMFIRST-n) ---------------- */
#define F3D_G_TRI1                0xBF /* gbi.h:138  G_IMMFIRST-0 */
#define F3D_G_CULLDL              0xBE /* gbi.h:139  G_IMMFIRST-1 */
#define F3D_G_POPMTX              0xBD /* gbi.h:140  G_IMMFIRST-2 */
#define F3D_G_MOVEWORD            0xBC /* gbi.h:141  G_IMMFIRST-3 */
#define F3D_G_TEXTURE             0xBB /* gbi.h:142  G_IMMFIRST-4 */
#define F3D_G_SETOTHERMODE_H      0xBA /* gbi.h:143  G_IMMFIRST-5 */
#define F3D_G_SETOTHERMODE_L      0xB9 /* gbi.h:144  G_IMMFIRST-6 */
#define F3D_G_ENDDL               0xB8 /* gbi.h:145  G_IMMFIRST-7 */
#define F3D_G_SETGEOMETRYMODE     0xB7 /* gbi.h:146  G_IMMFIRST-8 */
#define F3D_G_CLEARGEOMETRYMODE   0xB6 /* gbi.h:147  G_IMMFIRST-9 */
#define F3D_G_LINE3D              0xB5 /* gbi.h:148  G_IMMFIRST-10 */
#define F3D_G_RDPHALF_1           0xB4 /* gbi.h:149  G_IMMFIRST-11 */
#define F3D_G_RDPHALF_2           0xB3 /* gbi.h:150  G_IMMFIRST-12 */
#define F3D_G_RDPHALF_CONT        0xB2 /* gbi.h:157  G_IMMFIRST-13 (#else of F3DEX_GBI/F3DLP_GBI) */
/* Aliases overloaded onto CULLDL/POPMTX in the non-F3DEX branch (gbi.h:163-164). */
#define F3D_G_SPRITE2D_SCALEFLIP  0xBE /* gbi.h:163  alias of G_CULLDL */
#define F3D_G_SPRITE2D_DRAW       0xBD /* gbi.h:164  alias of G_POPMTX */

/* Assembly-only sprite tile names (gbi.h:380-381, inside _LANGUAGE_ASSEMBLY). */
#define F3D_G_TX_LOADTILE         7    /* gbi.h:380 */
#define F3D_G_TX_RENDERTILE       0    /* gbi.h:381 */

/* ---- Generated from gbi.h by gen.py (literal #defines, F3D_ prefixed) --- */

/* RSP DMA opcodes (Fast3D branch) */
#define F3D_G_SPNOOP 0x00 /* gbi.h:125 */
#define F3D_G_MTX 0x01 /* gbi.h:126 */
#define F3D_G_RESERVED0 0x02 /* gbi.h:127 */
#define F3D_G_MOVEMEM 0x03 /* gbi.h:128 */
#define F3D_G_VTX 0x04 /* gbi.h:129 */
#define F3D_G_RESERVED1 0x05 /* gbi.h:130 */
#define F3D_G_DL 0x06 /* gbi.h:131 */
#define F3D_G_RESERVED2 0x07 /* gbi.h:132 */
#define F3D_G_RESERVED3 0x08 /* gbi.h:133 */
#define F3D_G_SPRITE2D_BASE 0x09 /* gbi.h:134 */

/* RDP opcodes (raw byte values) */
#define F3D_G_NOOP 0xc0 /* gbi.h:167 */
#define F3D_G_SETCIMG 0xff /* gbi.h:172 */
#define F3D_G_SETZIMG 0xfe /* gbi.h:173 */
#define F3D_G_SETTIMG 0xfd /* gbi.h:174 */
#define F3D_G_SETCOMBINE 0xfc /* gbi.h:175 */
#define F3D_G_SETENVCOLOR 0xfb /* gbi.h:176 */
#define F3D_G_SETPRIMCOLOR 0xfa /* gbi.h:177 */
#define F3D_G_SETBLENDCOLOR 0xf9 /* gbi.h:178 */
#define F3D_G_SETFOGCOLOR 0xf8 /* gbi.h:179 */
#define F3D_G_SETFILLCOLOR 0xf7 /* gbi.h:180 */
#define F3D_G_FILLRECT 0xf6 /* gbi.h:181 */
#define F3D_G_SETTILE 0xf5 /* gbi.h:182 */
#define F3D_G_LOADTILE 0xf4 /* gbi.h:183 */
#define F3D_G_LOADBLOCK 0xf3 /* gbi.h:184 */
#define F3D_G_SETTILESIZE 0xf2 /* gbi.h:185 */
#define F3D_G_LOADTLUT 0xf0 /* gbi.h:186 */
#define F3D_G_RDPSETOTHERMODE 0xef /* gbi.h:187 */
#define F3D_G_SETPRIMDEPTH 0xee /* gbi.h:188 */
#define F3D_G_SETSCISSOR 0xed /* gbi.h:189 */
#define F3D_G_SETCONVERT 0xec /* gbi.h:190 */
#define F3D_G_SETKEYR 0xeb /* gbi.h:191 */
#define F3D_G_SETKEYGB 0xea /* gbi.h:192 */
#define F3D_G_RDPFULLSYNC 0xe9 /* gbi.h:193 */
#define F3D_G_RDPTILESYNC 0xe8 /* gbi.h:194 */
#define F3D_G_RDPPIPESYNC 0xe7 /* gbi.h:195 */
#define F3D_G_RDPLOADSYNC 0xe6 /* gbi.h:196 */
#define F3D_G_TEXRECTFLIP 0xe5 /* gbi.h:197 */
#define F3D_G_TEXRECT 0xe4 /* gbi.h:198 */

/* RSP-generated RDP triangle commands */
#define F3D_G_TRI_FILL 0xc8 /* gbi.h:209 */
#define F3D_G_TRI_SHADE 0xcc /* gbi.h:210 */
#define F3D_G_TRI_TXTR 0xca /* gbi.h:211 */
#define F3D_G_TRI_SHADE_TXTR 0xce /* gbi.h:212 */
#define F3D_G_TRI_FILL_ZBUFF 0xc9 /* gbi.h:213 */
#define F3D_G_TRI_SHADE_ZBUFF 0xcd /* gbi.h:214 */
#define F3D_G_TRI_TXTR_ZBUFF 0xcb /* gbi.h:215 */
#define F3D_G_TRI_SHADE_TXTR_ZBUFF 0xcf /* gbi.h:216 */

/* RDP triangle commands: edge/shade/texture/zbuffer masks */
#define F3D_G_RDP_TRI_FILL_MASK 0x08 /* gbi.h:232 */
#define F3D_G_RDP_TRI_SHADE_MASK 0x04 /* gbi.h:233 */
#define F3D_G_RDP_TRI_TXTR_MASK 0x02 /* gbi.h:234 */
#define F3D_G_RDP_TRI_ZBUFF_MASK 0x01 /* gbi.h:235 */

/* G_MTX parameter flags (Fast3D) */
#define F3D_G_MTX_MODELVIEW 0x00 /* gbi.h:307 */
#define F3D_G_MTX_PROJECTION 0x01 /* gbi.h:308 */
#define F3D_G_MTX_MUL 0x00 /* gbi.h:309 */
#define F3D_G_MTX_LOAD 0x02 /* gbi.h:310 */
#define F3D_G_MTX_NOPUSH 0x00 /* gbi.h:311 */
#define F3D_G_MTX_PUSH 0x04 /* gbi.h:312 */

/* G_DL parameter flags */
#define F3D_G_DL_PUSH 0x00 /* gbi.h:1026 */
#define F3D_G_DL_NOPUSH 0x01 /* gbi.h:1027 */

/* G_MOVEMEM indices (Fast3D) */
#define F3D_G_MV_VIEWPORT 0x80 /* gbi.h:1214 */
#define F3D_G_MV_LOOKATY 0x82 /* gbi.h:1215 */
#define F3D_G_MV_LOOKATX 0x84 /* gbi.h:1216 */
#define F3D_G_MV_L0 0x86 /* gbi.h:1217 */
#define F3D_G_MV_L1 0x88 /* gbi.h:1218 */
#define F3D_G_MV_L2 0x8a /* gbi.h:1219 */
#define F3D_G_MV_L3 0x8c /* gbi.h:1220 */
#define F3D_G_MV_L4 0x8e /* gbi.h:1221 */
#define F3D_G_MV_L5 0x90 /* gbi.h:1222 */
#define F3D_G_MV_L6 0x92 /* gbi.h:1223 */
#define F3D_G_MV_L7 0x94 /* gbi.h:1224 */
#define F3D_G_MV_TXTATT 0x96 /* gbi.h:1225 */
#define F3D_G_MV_MATRIX_1 0x9e /* gbi.h:1226 */
#define F3D_G_MV_MATRIX_2 0x98 /* gbi.h:1227 */
#define F3D_G_MV_MATRIX_3 0x9a /* gbi.h:1228 */
#define F3D_G_MV_MATRIX_4 0x9c /* gbi.h:1229 */

/* G_MOVEWORD indices (Fast3D) */
#define F3D_G_MW_MATRIX 0x00 /* gbi.h:1240 */
#define F3D_G_MW_NUMLIGHT 0x02 /* gbi.h:1241 */
#define F3D_G_MW_CLIP 0x04 /* gbi.h:1242 */
#define F3D_G_MW_SEGMENT 0x06 /* gbi.h:1243 */
#define F3D_G_MW_FOG 0x08 /* gbi.h:1244 */
#define F3D_G_MW_LIGHTCOL 0x0a /* gbi.h:1245 */
#define F3D_G_MW_POINTS 0x0c /* gbi.h:1249 */
#define F3D_G_MW_PERSPNORM 0x0e /* gbi.h:1251 */

/* G_MOVEWORD offsets (G_MWO_*, Fast3D) */
#define F3D_G_MWO_NUMLIGHT 0x00 /* gbi.h:1256 */
#define F3D_G_MWO_CLIP_RNX 0x04 /* gbi.h:1257 */
#define F3D_G_MWO_CLIP_RNY 0x0c /* gbi.h:1258 */
#define F3D_G_MWO_CLIP_RPX 0x14 /* gbi.h:1259 */
#define F3D_G_MWO_CLIP_RPY 0x1c /* gbi.h:1260 */
#define F3D_G_MWO_SEGMENT_0 0x00 /* gbi.h:1261 */
#define F3D_G_MWO_SEGMENT_1 0x01 /* gbi.h:1262 */
#define F3D_G_MWO_SEGMENT_2 0x02 /* gbi.h:1263 */
#define F3D_G_MWO_SEGMENT_3 0x03 /* gbi.h:1264 */
#define F3D_G_MWO_SEGMENT_4 0x04 /* gbi.h:1265 */
#define F3D_G_MWO_SEGMENT_5 0x05 /* gbi.h:1266 */
#define F3D_G_MWO_SEGMENT_6 0x06 /* gbi.h:1267 */
#define F3D_G_MWO_SEGMENT_7 0x07 /* gbi.h:1268 */
#define F3D_G_MWO_SEGMENT_8 0x08 /* gbi.h:1269 */
#define F3D_G_MWO_SEGMENT_9 0x09 /* gbi.h:1270 */
#define F3D_G_MWO_SEGMENT_A 0x0a /* gbi.h:1271 */
#define F3D_G_MWO_SEGMENT_B 0x0b /* gbi.h:1272 */
#define F3D_G_MWO_SEGMENT_C 0x0c /* gbi.h:1273 */
#define F3D_G_MWO_SEGMENT_D 0x0d /* gbi.h:1274 */
#define F3D_G_MWO_SEGMENT_E 0x0e /* gbi.h:1275 */
#define F3D_G_MWO_SEGMENT_F 0x0f /* gbi.h:1276 */
#define F3D_G_MWO_FOG 0x00 /* gbi.h:1277 */
#define F3D_G_MWO_aLIGHT_1 0x00 /* gbi.h:1278 */
#define F3D_G_MWO_bLIGHT_1 0x04 /* gbi.h:1279 */
#define F3D_G_MWO_aLIGHT_2 0x20 /* gbi.h:1296 */
#define F3D_G_MWO_bLIGHT_2 0x24 /* gbi.h:1297 */
#define F3D_G_MWO_aLIGHT_3 0x40 /* gbi.h:1298 */
#define F3D_G_MWO_bLIGHT_3 0x44 /* gbi.h:1299 */
#define F3D_G_MWO_aLIGHT_4 0x60 /* gbi.h:1300 */
#define F3D_G_MWO_bLIGHT_4 0x64 /* gbi.h:1301 */
#define F3D_G_MWO_aLIGHT_5 0x80 /* gbi.h:1302 */
#define F3D_G_MWO_bLIGHT_5 0x84 /* gbi.h:1303 */
#define F3D_G_MWO_aLIGHT_6 0xa0 /* gbi.h:1304 */
#define F3D_G_MWO_bLIGHT_6 0xa4 /* gbi.h:1305 */
#define F3D_G_MWO_aLIGHT_7 0xc0 /* gbi.h:1306 */
#define F3D_G_MWO_bLIGHT_7 0xc4 /* gbi.h:1307 */
#define F3D_G_MWO_aLIGHT_8 0xe0 /* gbi.h:1308 */
#define F3D_G_MWO_bLIGHT_8 0xe4 /* gbi.h:1309 */
#define F3D_G_MWO_MATRIX_XX_XY_I 0x00 /* gbi.h:1311 */
#define F3D_G_MWO_MATRIX_XZ_XW_I 0x04 /* gbi.h:1312 */
#define F3D_G_MWO_MATRIX_YX_YY_I 0x08 /* gbi.h:1313 */
#define F3D_G_MWO_MATRIX_YZ_YW_I 0x0c /* gbi.h:1314 */
#define F3D_G_MWO_MATRIX_ZX_ZY_I 0x10 /* gbi.h:1315 */
#define F3D_G_MWO_MATRIX_ZZ_ZW_I 0x14 /* gbi.h:1316 */
#define F3D_G_MWO_MATRIX_WX_WY_I 0x18 /* gbi.h:1317 */
#define F3D_G_MWO_MATRIX_WZ_WW_I 0x1c /* gbi.h:1318 */
#define F3D_G_MWO_MATRIX_XX_XY_F 0x20 /* gbi.h:1319 */
#define F3D_G_MWO_MATRIX_XZ_XW_F 0x24 /* gbi.h:1320 */
#define F3D_G_MWO_MATRIX_YX_YY_F 0x28 /* gbi.h:1321 */
#define F3D_G_MWO_MATRIX_YZ_YW_F 0x2c /* gbi.h:1322 */
#define F3D_G_MWO_MATRIX_ZX_ZY_F 0x30 /* gbi.h:1323 */
#define F3D_G_MWO_MATRIX_ZZ_ZW_F 0x34 /* gbi.h:1324 */
#define F3D_G_MWO_MATRIX_WX_WY_F 0x38 /* gbi.h:1325 */
#define F3D_G_MWO_MATRIX_WZ_WW_F 0x3c /* gbi.h:1326 */
#define F3D_G_MWO_POINT_RGBA 0x10 /* gbi.h:1327 */
#define F3D_G_MWO_POINT_ST 0x14 /* gbi.h:1328 */
#define F3D_G_MWO_POINT_XYSCREEN 0x18 /* gbi.h:1329 */
#define F3D_G_MWO_POINT_ZSCREEN 0x1c /* gbi.h:1330 */

/* Geometry mode bits (Fast3D) */
#define F3D_G_ZBUFFER 0x00000001 /* gbi.h:340 */
#define F3D_G_SHADE 0x00000004 /* gbi.h:341 */
#define F3D_G_TEXTURE_ENABLE 0x00000002 /* gbi.h:350 */
#define F3D_G_SHADING_SMOOTH 0x00000200 /* gbi.h:351 */
#define F3D_G_CULL_FRONT 0x00001000 /* gbi.h:352 */
#define F3D_G_CULL_BACK 0x00002000 /* gbi.h:353 */
#define F3D_G_CULL_BOTH 0x00003000 /* gbi.h:354 */
#define F3D_G_FOG 0x00010000 /* gbi.h:356 */
#define F3D_G_LIGHTING 0x00020000 /* gbi.h:357 */
#define F3D_G_TEXTURE_GEN 0x00040000 /* gbi.h:358 */
#define F3D_G_TEXTURE_GEN_LINEAR 0x00080000 /* gbi.h:359 */
#define F3D_G_LOD 0x00100000 /* gbi.h:360 */
#define F3D_G_CLIPPING 0x00000000 /* gbi.h:364 */

/* Texture fraction bits */
#define F3D_G_TEXTURE_IMAGE_FRAC 0x02 /* gbi.h:271 */
#define F3D_G_TEXTURE_SCALE_FRAC 0x10 /* gbi.h:272 */
#define F3D_G_SCALE_FRAC 0x08 /* gbi.h:273 */
#define F3D_G_ROTATE_FRAC 0x10 /* gbi.h:274 */

/* G_SETIMG fmt */
#define F3D_G_IM_FMT_RGBA 0x00 /* gbi.h:394 */
#define F3D_G_IM_FMT_YUV 0x01 /* gbi.h:395 */
#define F3D_G_IM_FMT_CI 0x02 /* gbi.h:396 */
#define F3D_G_IM_FMT_IA 0x03 /* gbi.h:397 */
#define F3D_G_IM_FMT_I 0x04 /* gbi.h:398 */

/* G_SETIMG siz */
#define F3D_G_IM_SIZ_4b 0x00 /* gbi.h:403 */
#define F3D_G_IM_SIZ_8b 0x01 /* gbi.h:404 */
#define F3D_G_IM_SIZ_16b 0x02 /* gbi.h:405 */
#define F3D_G_IM_SIZ_32b 0x03 /* gbi.h:406 */
#define F3D_G_IM_SIZ_DD 0x05 /* gbi.h:407 */

/* Color combiner selectors, RGB (G_CCMUX_*) */
#define F3D_G_CCMUX_COMBINED 0x00 /* gbi.h:444 */
#define F3D_G_CCMUX_TEXEL0 0x01 /* gbi.h:445 */
#define F3D_G_CCMUX_TEXEL1 0x02 /* gbi.h:446 */
#define F3D_G_CCMUX_PRIMITIVE 0x03 /* gbi.h:447 */
#define F3D_G_CCMUX_SHADE 0x04 /* gbi.h:448 */
#define F3D_G_CCMUX_ENVIRONMENT 0x05 /* gbi.h:449 */
#define F3D_G_CCMUX_CENTER 0x06 /* gbi.h:450 */
#define F3D_G_CCMUX_SCALE 0x06 /* gbi.h:451 */
#define F3D_G_CCMUX_COMBINED_ALPHA 0x07 /* gbi.h:452 */
#define F3D_G_CCMUX_TEXEL0_ALPHA 0x08 /* gbi.h:453 */
#define F3D_G_CCMUX_TEXEL1_ALPHA 0x09 /* gbi.h:454 */
#define F3D_G_CCMUX_PRIMITIVE_ALPHA 0x0A /* gbi.h:455 */
#define F3D_G_CCMUX_SHADE_ALPHA 0x0B /* gbi.h:456 */
#define F3D_G_CCMUX_ENV_ALPHA 0x0C /* gbi.h:457 */
#define F3D_G_CCMUX_LOD_FRACTION 0x0D /* gbi.h:458 */
#define F3D_G_CCMUX_PRIM_LOD_FRAC 0x0E /* gbi.h:459 */
#define F3D_G_CCMUX_NOISE 0x07 /* gbi.h:460 */
#define F3D_G_CCMUX_K4 0x07 /* gbi.h:461 */
#define F3D_G_CCMUX_K5 0x0F /* gbi.h:462 */
#define F3D_G_CCMUX_1 0x06 /* gbi.h:463 */
#define F3D_G_CCMUX_0 0x1F /* gbi.h:464 */

/* Alpha combiner selectors (G_ACMUX_*) */
#define F3D_G_ACMUX_COMBINED 0x00 /* gbi.h:467 */
#define F3D_G_ACMUX_TEXEL0 0x01 /* gbi.h:468 */
#define F3D_G_ACMUX_TEXEL1 0x02 /* gbi.h:469 */
#define F3D_G_ACMUX_PRIMITIVE 0x03 /* gbi.h:470 */
#define F3D_G_ACMUX_SHADE 0x04 /* gbi.h:471 */
#define F3D_G_ACMUX_ENVIRONMENT 0x05 /* gbi.h:472 */
#define F3D_G_ACMUX_LOD_FRACTION 0x00 /* gbi.h:473 */
#define F3D_G_ACMUX_PRIM_LOD_FRAC 0x06 /* gbi.h:474 */
#define F3D_G_ACMUX_1 0x06 /* gbi.h:475 */
#define F3D_G_ACMUX_0 0x07 /* gbi.h:476 */

/* G_SETOTHERMODE shift counts (G_MDSFT_*) */
#define F3D_G_MDSFT_ALPHACOMPARE 0x00 /* gbi.h:560 */
#define F3D_G_MDSFT_ZSRCSEL 0x02 /* gbi.h:561 */
#define F3D_G_MDSFT_RENDERMODE 0x03 /* gbi.h:562 */
#define F3D_G_MDSFT_BLENDER 0x10 /* gbi.h:563 */
#define F3D_G_MDSFT_BLENDMASK 0x00 /* gbi.h:568 */
#define F3D_G_MDSFT_ALPHADITHER 0x04 /* gbi.h:569 */
#define F3D_G_MDSFT_RGBDITHER 0x06 /* gbi.h:570 */
#define F3D_G_MDSFT_COMBKEY 0x08 /* gbi.h:572 */
#define F3D_G_MDSFT_TEXTCONV 0x09 /* gbi.h:573 */
#define F3D_G_MDSFT_TEXTFILT 0x0C /* gbi.h:574 */
#define F3D_G_MDSFT_TEXTLUT 0x0E /* gbi.h:575 */
#define F3D_G_MDSFT_TEXTLOD 0x10 /* gbi.h:576 */
#define F3D_G_MDSFT_TEXTDETAIL 0x11 /* gbi.h:577 */
#define F3D_G_MDSFT_TEXTPERSP 0x13 /* gbi.h:578 */
#define F3D_G_MDSFT_CYCLETYPE 0x14 /* gbi.h:579 */
#define F3D_G_MDSFT_COLORDITHER 0x16 /* gbi.h:580 */
#define F3D_G_MDSFT_PIPELINE 0x17 /* gbi.h:581 */

/* Render mode bits (othermode-L, RM flags) */
#define F3D_AA_EN 0x8 /* gbi.h:654 */
#define F3D_Z_CMP 0x10 /* gbi.h:655 */
#define F3D_Z_UPD 0x20 /* gbi.h:656 */
#define F3D_IM_RD 0x40 /* gbi.h:657 */
#define F3D_CLR_ON_CVG 0x80 /* gbi.h:658 */
#define F3D_CVG_DST_CLAMP 0x00 /* gbi.h:659 */
#define F3D_CVG_DST_WRAP 0x100 /* gbi.h:660 */
#define F3D_CVG_DST_FULL 0x200 /* gbi.h:661 */
#define F3D_CVG_DST_SAVE 0x300 /* gbi.h:662 */
#define F3D_ZMODE_OPA 0x00 /* gbi.h:663 */
#define F3D_ZMODE_INTER 0x400 /* gbi.h:664 */
#define F3D_ZMODE_XLU 0x800 /* gbi.h:665 */
#define F3D_ZMODE_DEC 0xc00 /* gbi.h:666 */
#define F3D_CVG_X_ALPHA 0x1000 /* gbi.h:667 */
#define F3D_ALPHA_CVG_SEL 0x2000 /* gbi.h:668 */
#define F3D_FORCE_BL 0x4000 /* gbi.h:669 */
#define F3D_TEX_EDGE 0x0000 /* gbi.h:670 */

/* Blender selectors (G_BL_*) */
#define F3D_G_BL_CLR_IN 0x00 /* gbi.h:672 */
#define F3D_G_BL_CLR_MEM 0x01 /* gbi.h:673 */
#define F3D_G_BL_CLR_BL 0x02 /* gbi.h:674 */
#define F3D_G_BL_CLR_FOG 0x03 /* gbi.h:675 */
#define F3D_G_BL_1MA 0x00 /* gbi.h:676 */
#define F3D_G_BL_A_MEM 0x01 /* gbi.h:677 */
#define F3D_G_BL_A_IN 0x00 /* gbi.h:678 */
#define F3D_G_BL_A_FOG 0x01 /* gbi.h:679 */
#define F3D_G_BL_A_SHADE 0x02 /* gbi.h:680 */
#define F3D_G_BL_1 0x02 /* gbi.h:681 */
#define F3D_G_BL_0 0x03 /* gbi.h:682 */

/* LOADBLOCK limits, DXT */
#define F3D_G_TX_DXT_FRAC 0x0B /* gbi.h:3248 */
#define F3D_G_TX_LDBLK_MAX_TXL 2047 /* gbi.h:3263 */

/* ---- G_SETOTHERMODE_H / _L field values (gbi.h:583-670, shift expressions) ---- */
#define F3D_G_PM_1PRIMITIVE       (1 << F3D_G_MDSFT_PIPELINE)     /* gbi.h:584 */
#define F3D_G_PM_NPRIMITIVE       (0 << F3D_G_MDSFT_PIPELINE)     /* gbi.h:585 */
#define F3D_G_CYC_1CYCLE          (0 << F3D_G_MDSFT_CYCLETYPE)    /* gbi.h:588 */
#define F3D_G_CYC_2CYCLE          (1 << F3D_G_MDSFT_CYCLETYPE)    /* gbi.h:589 */
#define F3D_G_CYC_COPY            (2 << F3D_G_MDSFT_CYCLETYPE)    /* gbi.h:590 */
#define F3D_G_CYC_FILL            (3 << F3D_G_MDSFT_CYCLETYPE)    /* gbi.h:591 */
#define F3D_G_TP_NONE             (0 << F3D_G_MDSFT_TEXTPERSP)    /* gbi.h:594 */
#define F3D_G_TP_PERSP            (1 << F3D_G_MDSFT_TEXTPERSP)    /* gbi.h:595 */
#define F3D_G_TD_CLAMP            (0 << F3D_G_MDSFT_TEXTDETAIL)   /* gbi.h:598 */
#define F3D_G_TD_SHARPEN          (1 << F3D_G_MDSFT_TEXTDETAIL)   /* gbi.h:599 */
#define F3D_G_TD_DETAIL           (2 << F3D_G_MDSFT_TEXTDETAIL)   /* gbi.h:600 */
#define F3D_G_TL_TILE             (0 << F3D_G_MDSFT_TEXTLOD)      /* gbi.h:603 */
#define F3D_G_TL_LOD              (1 << F3D_G_MDSFT_TEXTLOD)      /* gbi.h:604 */
#define F3D_G_TT_NONE             (0 << F3D_G_MDSFT_TEXTLUT)      /* gbi.h:607 */
#define F3D_G_TT_RGBA16           (2 << F3D_G_MDSFT_TEXTLUT)      /* gbi.h:608 */
#define F3D_G_TT_IA16             (3 << F3D_G_MDSFT_TEXTLUT)      /* gbi.h:609 */
#define F3D_G_TF_POINT            (0 << F3D_G_MDSFT_TEXTFILT)     /* gbi.h:612 */
#define F3D_G_TF_AVERAGE          (3 << F3D_G_MDSFT_TEXTFILT)     /* gbi.h:613 */
#define F3D_G_TF_BILERP           (2 << F3D_G_MDSFT_TEXTFILT)     /* gbi.h:614 */
#define F3D_G_TC_CONV             (0 << F3D_G_MDSFT_TEXTCONV)     /* gbi.h:617 */
#define F3D_G_TC_FILTCONV         (5 << F3D_G_MDSFT_TEXTCONV)     /* gbi.h:618 */
#define F3D_G_TC_FILT             (6 << F3D_G_MDSFT_TEXTCONV)     /* gbi.h:619 */
#define F3D_G_CK_NONE             (0 << F3D_G_MDSFT_COMBKEY)      /* gbi.h:622 */
#define F3D_G_CK_KEY              (1 << F3D_G_MDSFT_COMBKEY)      /* gbi.h:623 */
#define F3D_G_CD_MAGICSQ          (0 << F3D_G_MDSFT_RGBDITHER)    /* gbi.h:626 */
#define F3D_G_CD_BAYER            (1 << F3D_G_MDSFT_RGBDITHER)    /* gbi.h:627 */
#define F3D_G_CD_NOISE            (2 << F3D_G_MDSFT_RGBDITHER)    /* gbi.h:628 */
#define F3D_G_CD_DISABLE          (3 << F3D_G_MDSFT_RGBDITHER)    /* gbi.h:631 (not _HW_VERSION_1) */
#define F3D_G_CD_ENABLE           F3D_G_CD_NOISE                  /* gbi.h:632 */
#define F3D_G_AD_PATTERN          (0 << F3D_G_MDSFT_ALPHADITHER)  /* gbi.h:639 */
#define F3D_G_AD_NOTPATTERN       (1 << F3D_G_MDSFT_ALPHADITHER)  /* gbi.h:640 */
#define F3D_G_AD_NOISE            (2 << F3D_G_MDSFT_ALPHADITHER)  /* gbi.h:641 */
#define F3D_G_AD_DISABLE          (3 << F3D_G_MDSFT_ALPHADITHER)  /* gbi.h:642 */
#define F3D_G_AC_NONE             (0 << F3D_G_MDSFT_ALPHACOMPARE) /* gbi.h:645 */
#define F3D_G_AC_THRESHOLD        (1 << F3D_G_MDSFT_ALPHACOMPARE) /* gbi.h:646 */
#define F3D_G_AC_DITHER           (3 << F3D_G_MDSFT_ALPHACOMPARE) /* gbi.h:647 */
#define F3D_G_ZS_PIXEL            (0 << F3D_G_MDSFT_ZSRCSEL)      /* gbi.h:650 */
#define F3D_G_ZS_PRIM             (1 << F3D_G_MDSFT_ZSRCSEL)      /* gbi.h:651 */

/* ---- Blender: GBL_c1 / GBL_c2 (gbi.h:684-687). Cycle 1 sits at bits 30..18, cycle 2 at 28..16. */
#define F3D_GBL_C1(m1a, m1b, m2a, m2b) \
    (((m1a) << 30) | ((m1b) << 26) | ((m2a) << 22) | ((m2b) << 18)) /* gbi.h:684 */
#define F3D_GBL_C2(m1a, m1b, m2a, m2b) \
    (((m1a) << 28) | ((m1b) << 24) | ((m2a) << 20) | ((m2b) << 16)) /* gbi.h:686 */

/* ---- Render-mode composites (gbi.h:689-1006). clk is 1 or 2 (cycle), pasted onto F3D_GBL_C. */
#define F3D_RM_AA_ZB_OPA_SURF(clk) (F3D_AA_EN | F3D_Z_CMP | F3D_Z_UPD | F3D_IM_RD | \
    F3D_CVG_DST_CLAMP | F3D_ZMODE_OPA | F3D_ALPHA_CVG_SEL | \
    F3D_GBL_C##clk(F3D_G_BL_CLR_IN, F3D_G_BL_A_IN, F3D_G_BL_CLR_MEM, F3D_G_BL_A_MEM)) /* gbi.h:689 */
#define F3D_RM_AA_ZB_XLU_SURF(clk) (F3D_AA_EN | F3D_Z_CMP | F3D_IM_RD | F3D_CVG_DST_WRAP | \
    F3D_CLR_ON_CVG | F3D_FORCE_BL | F3D_ZMODE_XLU | \
    F3D_GBL_C##clk(F3D_G_BL_CLR_IN, F3D_G_BL_A_IN, F3D_G_BL_CLR_MEM, F3D_G_BL_1MA)) /* gbi.h:699 */
#define F3D_RM_AA_ZB_OPA_DECAL(clk) (F3D_AA_EN | F3D_Z_CMP | F3D_IM_RD | F3D_CVG_DST_WRAP | \
    F3D_ALPHA_CVG_SEL | F3D_ZMODE_DEC | \
    F3D_GBL_C##clk(F3D_G_BL_CLR_IN, F3D_G_BL_A_IN, F3D_G_BL_CLR_MEM, F3D_G_BL_A_MEM)) /* gbi.h:704 */
#define F3D_RM_AA_ZB_XLU_DECAL(clk) (F3D_AA_EN | F3D_Z_CMP | F3D_IM_RD | F3D_CVG_DST_WRAP | \
    F3D_CLR_ON_CVG | F3D_FORCE_BL | F3D_ZMODE_DEC | \
    F3D_GBL_C##clk(F3D_G_BL_CLR_IN, F3D_G_BL_A_IN, F3D_G_BL_CLR_MEM, F3D_G_BL_1MA)) /* gbi.h:714 */
#define F3D_RM_AA_ZB_OPA_TERR(clk) (F3D_AA_EN | F3D_Z_CMP | F3D_Z_UPD | F3D_IM_RD | \
    F3D_CVG_DST_CLAMP | F3D_ZMODE_OPA | F3D_ALPHA_CVG_SEL | \
    F3D_GBL_C##clk(F3D_G_BL_CLR_IN, F3D_G_BL_A_IN, F3D_G_BL_CLR_MEM, F3D_G_BL_1MA)) /* gbi.h:764 */
#define F3D_RM_AA_ZB_TEX_TERR(clk) (F3D_AA_EN | F3D_Z_CMP | F3D_Z_UPD | F3D_IM_RD | \
    F3D_CVG_DST_CLAMP | F3D_CVG_X_ALPHA | F3D_ALPHA_CVG_SEL | F3D_ZMODE_OPA | F3D_TEX_EDGE | \
    F3D_GBL_C##clk(F3D_G_BL_CLR_IN, F3D_G_BL_A_IN, F3D_G_BL_CLR_MEM, F3D_G_BL_1MA)) /* gbi.h:769 */
#define F3D_RM_AA_OPA_SURF(clk) (F3D_AA_EN | F3D_IM_RD | F3D_CVG_DST_CLAMP | \
    F3D_ZMODE_OPA | F3D_ALPHA_CVG_SEL | \
    F3D_GBL_C##clk(F3D_G_BL_CLR_IN, F3D_G_BL_A_IN, F3D_G_BL_CLR_MEM, F3D_G_BL_A_MEM)) /* gbi.h:780 */
#define F3D_RM_AA_XLU_SURF(clk) (F3D_AA_EN | F3D_IM_RD | F3D_CVG_DST_WRAP | F3D_CLR_ON_CVG | \
    F3D_FORCE_BL | F3D_ZMODE_OPA | \
    F3D_GBL_C##clk(F3D_G_BL_CLR_IN, F3D_G_BL_A_IN, F3D_G_BL_CLR_MEM, F3D_G_BL_1MA)) /* gbi.h:790 */
#define F3D_RM_OPA_SURF(clk) (F3D_CVG_DST_CLAMP | F3D_FORCE_BL | F3D_ZMODE_OPA | \
    F3D_GBL_C##clk(F3D_G_BL_CLR_IN, F3D_G_BL_0, F3D_G_BL_CLR_IN, F3D_G_BL_1)) /* gbi.h:867 */
#define F3D_RM_XLU_SURF(clk) (F3D_IM_RD | F3D_CVG_DST_FULL | F3D_FORCE_BL | F3D_ZMODE_OPA | \
    F3D_GBL_C##clk(F3D_G_BL_CLR_IN, F3D_G_BL_A_IN, F3D_G_BL_CLR_MEM, F3D_G_BL_1MA)) /* gbi.h:871 */
#define F3D_G_RM_FOG_SHADE_A  F3D_GBL_C1(F3D_G_BL_CLR_FOG, F3D_G_BL_A_SHADE, F3D_G_BL_CLR_IN, F3D_G_BL_1MA) /* gbi.h:1004 */
#define F3D_G_RM_FOG_PRIM_A   F3D_GBL_C1(F3D_G_BL_CLR_FOG, F3D_G_BL_A_FOG, F3D_G_BL_CLR_IN, F3D_G_BL_1MA)  /* gbi.h:1005 */
#define F3D_G_RM_PASS         F3D_GBL_C1(F3D_G_BL_CLR_IN, F3D_G_BL_0, F3D_G_BL_CLR_IN, F3D_G_BL_1)          /* gbi.h:1006 */

/* ======================================================================
 * Field decoders. Each takes the raw words of one Gfx (w0 = high word,
 * w1 = low word) and writes the fields the gbi.h macro packed in.
 * Bit positions are listed per helper. Decoders are static inline, so the
 * header adds no link-time symbols.
 * ====================================================================== */

/* Opcode byte (w0 bits 24..31). */
static inline uint8_t f3d_op(uint32_t w0) { return (uint8_t)(w0 >> 24); }

/* ---- Segmented addresses (gSPSegment / SEGMENT_MOVEWORD; interp.rs resolve) ----
 * addr = seg << 24 | offset, seg in 0..15 (bits 24..27), offset bits 0..23. */
static inline uint8_t f3d_seg_index(uint32_t addr) { return (uint8_t)((addr >> 24) & 0x0Fu); }
static inline uint32_t f3d_seg_offset(uint32_t addr) { return addr & 0x00FFFFFFu; }
/* Physical address through a 16-entry segment base table (gSPSegment bases). */
static inline uint32_t f3d_seg_resolve(const uint32_t segs[16], uint32_t addr)
{
    return (segs[f3d_seg_index(addr)] + f3d_seg_offset(addr)) & 0x1FFFFFFFu;
}

/* ---- G_VTX (gbi.h:1801 non-F3DEX): w0 = (((n-1)<<4 | v0) << 16) | n*16, w1 = addr ---- */
static inline void f3d_vtx_decode(uint32_t w0, uint32_t w1,
                                  uint32_t *n, uint32_t *v0, uint32_t *len, uint32_t *addr)
{
    *n = ((w0 >> 20) & 0xFu) + 1u;  /* bits 20..23 hold n-1 */
    *v0 = (w0 >> 16) & 0xFu;        /* bits 16..19: first cache slot */
    *len = w0 & 0xFFFFu;            /* bits 0..15: n * sizeof(Vtx) = n*16 */
    *addr = w1;                     /* segmented address of Vtx[] */
}

/* ---- G_TRI1 (gbi.h:1978 non-F3DEX, __gsSP1Triangle_w1f):
 * w1 = flag << 24 | (v0*10) << 16 | (v1*10) << 8 | (v2*10).
 * Indices are stored times 10, so decode divides by 10. flag is the
 * provoking-vertex index that interp.rs uses for flat shading. */
static inline uint32_t f3d_tri1_encode(uint8_t v0, uint8_t v1, uint8_t v2, uint8_t flag)
{
    return ((uint32_t)flag << 24) | ((uint32_t)v0 * 10u << 16) |
           ((uint32_t)v1 * 10u << 8) | ((uint32_t)v2 * 10u);
}
static inline void f3d_tri1_decode(uint32_t w1, uint8_t *a, uint8_t *b, uint8_t *c, uint8_t *flag)
{
    *a = (uint8_t)(((w1 >> 16) & 0xFFu) / 10u);
    *b = (uint8_t)(((w1 >> 8) & 0xFFu) / 10u);
    *c = (uint8_t)((w1 & 0xFFu) / 10u);
    *flag = (uint8_t)(w1 >> 24);
}

/* ---- G_LINE3D (gbi.h:2000+ __gsSPLine3D_w1f): flag<<24 | v0*10<<16 | v1*10<<8 | wd. */
static inline void f3d_line3d_decode(uint32_t w1, uint8_t *v0, uint8_t *v1, uint8_t *wd, uint8_t *flag)
{
    *v0 = (uint8_t)(((w1 >> 16) & 0xFFu) / 10u);
    *v1 = (uint8_t)(((w1 >> 8) & 0xFFu) / 10u);
    *wd = (uint8_t)(w1 & 0xFFu);
    *flag = (uint8_t)(w1 >> 24);
}

/* ---- G_CULLDL (gbi.h:2176 non-F3DEX): w0 = CULLDL<<24 | (vstart&0xF)*40,
 * w1 = ((vend+1)&0xF)*40. Multiples of 40 bytes per Vtx-cache slot. */
static inline void f3d_culldl_decode(uint32_t w0, uint32_t w1, uint8_t *vstart, uint8_t *vend)
{
    *vstart = (uint8_t)((w0 & 0xFFFFu) / 40u);
    *vend = (uint8_t)(((w1 / 40u) + 15u) & 0xFu); /* (vend+1) stored, so subtract 1 mod 16 */
}

/* ---- G_MTX (gbi.h:1759): w0 = MTX<<24 | params<<16 | len, w1 = addr.
 * params: PROJECTION 0x01, LOAD 0x02, PUSH 0x04 (F3D_G_MTX_*). len = 64 for Mtx. */
static inline void f3d_mtx_decode(uint32_t w0, uint32_t w1,
                                  uint8_t *params, uint32_t *len, uint32_t *addr)
{
    *params = (uint8_t)((w0 >> 16) & 0xFFu);
    *len = w0 & 0xFFFFu;
    *addr = w1;
}

/* ---- G_POPMTX (gbi.h:1845 gImmp1): w1 = number of pops. ---- */
static inline uint32_t f3d_popmtx_count(uint32_t w1) { return w1; }

/* ---- G_DL (gbi.h:1819): w0 bits 16..23 = push flag (0 push, 1 branch), w1 = addr. ---- */
static inline void f3d_dl_decode(uint32_t w0, uint32_t w1, uint8_t *push, uint32_t *addr)
{
    *push = (((w0 >> 16) & 0xFFu) == F3D_G_DL_PUSH);
    *addr = w1;
}

/* ---- G_MOVEMEM (gbi.h:1814 gDma1p): w0 = MOVEMEM<<24 | index<<16 | size, w1 = addr. ---- */
static inline void f3d_movemem_decode(uint32_t w0, uint32_t w1,
                                      uint8_t *index, uint32_t *len, uint32_t *addr)
{
    *index = (uint8_t)((w0 >> 16) & 0xFFu);  /* F3D_G_MV_* */
    *len = w0 & 0xFFFFu;
    *addr = w1;
}

/* ---- G_MOVEWORD (gbi.h:1906 gImmp21): w0 = MOVEWORD<<24 | offset<<8 | index, w1 = data. ---- */
static inline void f3d_moveword_decode(uint32_t w0, uint32_t w1,
                                       uint8_t *index, uint16_t *offset, uint32_t *data)
{
    *index = (uint8_t)(w0 & 0xFFu);          /* F3D_G_MW_* */
    *offset = (uint16_t)((w0 >> 8) & 0xFFFFu); /* F3D_G_MWO_* (may be a list index) */
    *data = w1;
}
/* G_MW_NUMLIGHT data (gbi.h:2445 NUML(n) = (n+1)*32 + 0x80000000) -> n. */
static inline uint8_t f3d_numlights_from_data(uint32_t data)
{
    return (uint8_t)(((data & 0x7FFFFFFFu) / 32u) - 1u);
}
/* G_MW_CLIP (gSPClipRatio): ratio is the low 16 bits of the RNX word (FR_NEG_FRUSTRATIO_n = n). */
static inline uint16_t f3d_clip_ratio_from_data(uint32_t data) { return (uint16_t)(data & 0xFFFFu); }
/* G_MW_FOG (gSPFogPosition): fm = data>>16, fo = low 16 bits, both s16 in interp.rs. */
static inline void f3d_fog_from_data(uint32_t data, int16_t *fm, int16_t *fo)
{
    *fm = (int16_t)(uint16_t)(data >> 16);
    *fo = (int16_t)(uint16_t)(data & 0xFFFFu);
}
/* G_MW_LIGHTCOL (gSPLightColor): offset = aLIGHT_n = (n-1)*0x20, bLIGHT_n = aLIGHT_n + 4.
 * Returns the 0-based light index, or 0xFF when the offset is not a light slot. */
static inline uint8_t f3d_lightcol_index(uint16_t offset)
{
    if ((offset & 0x1Fu) != 0u || offset >= 8u * 0x20u) return 0xFFu;
    return (uint8_t)(offset / 0x20u);
}

/* ---- G_TEXTURE (gbi.h:1967 non-F3DEX gSPTexture):
 * w0 = TEXTURE<<24 | BOWTIE(0)<<16 | level<<11 | tile<<8 | on (bits 0..7);
 * w1 = s<<16 | t (scale, 0.16 fixed; 0xFFFF in interp.rs means 1.0). */
static inline void f3d_texture_decode(uint32_t w0, uint32_t w1,
                                      uint8_t *level, uint8_t *tile, uint8_t *on,
                                      uint16_t *s, uint16_t *t)
{
    *level = (uint8_t)((w0 >> 11) & 0x7u);  /* levels-1 (LOD clamp) */
    *tile = (uint8_t)((w0 >> 8) & 0x7u);
    *on = (uint8_t)(w0 & 0xFFu);            /* non-zero = on */
    *s = (uint16_t)(w1 >> 16);
    *t = (uint16_t)(w1 & 0xFFFFu);
}

/* ---- G_SETOTHERMODE_H / _L (gbi.h:2890-2903 non-F3DEX):
 * w0 = cmd<<24 | sft<<8 | len, w1 = data already in place.
 * sft is the RDP register bit position (not 32-sft-len: that is F3DEX2). */
static inline uint8_t f3d_othermode_is_h(uint32_t w0) { return f3d_op(w0) == F3D_G_SETOTHERMODE_H; }
static inline void f3d_othermode_decode(uint32_t w0, uint32_t w1,
                                        uint8_t *sft, uint8_t *len, uint32_t *data)
{
    *sft = (uint8_t)((w0 >> 8) & 0xFFu);
    *len = (uint8_t)(w0 & 0xFFu);
    *data = w1;
}
/* Mask of the bits the command writes: ((1<<len)-1) << sft (interp.rs). len 0 -> 0. */
static inline uint32_t f3d_othermode_mask(uint32_t w0)
{
    uint32_t sft = (w0 >> 8) & 0xFFu;
    uint32_t len = w0 & 0xFFu;
    uint32_t m = (len >= 32u) ? 0xFFFFFFFFu : ((1u << len) - 1u);
    return (sft >= 32u) ? 0u : (m << sft);
}
/* Apply to a shadow register: reg = (reg & ~mask) | (w1 & mask). */
static inline uint32_t f3d_othermode_apply(uint32_t reg, uint32_t w0, uint32_t w1)
{
    uint32_t m = f3d_othermode_mask(w0);
    return (reg & ~m) | (w1 & m);
}

/* Fields of the OTHERMODE_H shadow word (bit positions from gbi.h:570-581). */
static inline uint8_t f3d_omh_alpha_dither(uint32_t h) { return (uint8_t)((h >> 4) & 3u); }
static inline uint8_t f3d_omh_rgb_dither(uint32_t h) { return (uint8_t)((h >> 6) & 3u); }
static inline uint8_t f3d_omh_comb_key(uint32_t h) { return (uint8_t)((h >> 8) & 1u); }
static inline uint8_t f3d_omh_text_conv(uint32_t h) { return (uint8_t)((h >> 9) & 7u); }
static inline uint8_t f3d_omh_text_filt(uint32_t h) { return (uint8_t)((h >> 12) & 3u); }
static inline uint8_t f3d_omh_text_lut(uint32_t h) { return (uint8_t)((h >> 14) & 3u); }
static inline uint8_t f3d_omh_text_lod(uint32_t h) { return (uint8_t)((h >> 16) & 1u); }
static inline uint8_t f3d_omh_text_detail(uint32_t h) { return (uint8_t)((h >> 17) & 3u); }
static inline uint8_t f3d_omh_text_persp(uint32_t h) { return (uint8_t)((h >> 19) & 1u); }
static inline uint8_t f3d_omh_cycle_type(uint32_t h) { return (uint8_t)((h >> 20) & 3u); } /* 0 1cyc,1 2cyc,2 copy,3 fill */
static inline uint8_t f3d_omh_pipeline(uint32_t h) { return (uint8_t)((h >> 23) & 1u); }

/* Fields of the OTHERMODE_L shadow word. Render mode occupies bits 3..31. */
static inline uint8_t f3d_oml_alpha_compare(uint32_t l) { return (uint8_t)(l & 3u); }
static inline uint8_t f3d_oml_depth_source(uint32_t l) { return (uint8_t)((l >> 2) & 1u); }
static inline uint32_t f3d_oml_render_mode(uint32_t l) { return l & 0xFFFFFFF8u; }
/* Blender selectors of one cycle (1 or 2): m1a, m1b, m2a, m2b (gbi.h:684-687). */
static inline void f3d_rm_blend_decode(uint32_t rm, uint8_t cycle,
                                       uint8_t *m1a, uint8_t *m1b, uint8_t *m2a, uint8_t *m2b)
{
    if (cycle == 1u) {
        *m1a = (uint8_t)((rm >> 30) & 3u); *m1b = (uint8_t)((rm >> 26) & 3u);
        *m2a = (uint8_t)((rm >> 22) & 3u); *m2b = (uint8_t)((rm >> 18) & 3u);
    } else {
        *m1a = (uint8_t)((rm >> 28) & 3u); *m1b = (uint8_t)((rm >> 24) & 3u);
        *m2a = (uint8_t)((rm >> 20) & 3u); *m2b = (uint8_t)((rm >> 16) & 3u);
    }
}

/* ---- G_SETCOMBINE (gbi.h:3045-3095). w0 = FC<<24 | cycle-0 and cycle-1
 * RGB a/c and alpha Aa/Ac (low 24 bits), w1 = b/d/Ab/Ad and cycle-1 b/Aa/Ac/d/Ab/Ad.
 *
 *   cycle 0 (w0): a0 = bits 20..23, c0 = 15..19, Aa0 = 12..14, Ac0 = 9..11
 *   cycle 0 (w1): b0 = bits 28..31, d0 = 15..17, Ab0 = 12..14, Ad0 = 9..11
 *   cycle 1 (w0): a1 = bits 5..8,   c1 = 0..4,   (w1): b1 = 24..27, Aa1 = 21..23,
 *                 Ac1 = 18..20, d1 = 6..8, Ab1 = 3..5, Ad1 = 0..2
 *
 * Field values are the G_CCMUX_* (RGB) / G_ACMUX_* (alpha) constants. */
static inline uint8_t f3d_cc_a0(uint32_t w0, uint32_t w1) { (void)w1; return (uint8_t)((w0 >> 20) & 0xFu); }
static inline uint8_t f3d_cc_b0(uint32_t w0, uint32_t w1) { (void)w0; return (uint8_t)((w1 >> 28) & 0xFu); }
static inline uint8_t f3d_cc_c0(uint32_t w0, uint32_t w1) { (void)w1; return (uint8_t)((w0 >> 15) & 0x1Fu); }
static inline uint8_t f3d_cc_d0(uint32_t w0, uint32_t w1) { (void)w0; return (uint8_t)((w1 >> 15) & 0x7u); }
static inline uint8_t f3d_cc_Aa0(uint32_t w0, uint32_t w1) { (void)w1; return (uint8_t)((w0 >> 12) & 0x7u); }
static inline uint8_t f3d_cc_Ab0(uint32_t w0, uint32_t w1) { (void)w0; return (uint8_t)((w1 >> 12) & 0x7u); }
static inline uint8_t f3d_cc_Ac0(uint32_t w0, uint32_t w1) { (void)w1; return (uint8_t)((w0 >> 9) & 0x7u); }
static inline uint8_t f3d_cc_Ad0(uint32_t w0, uint32_t w1) { (void)w0; return (uint8_t)((w1 >> 9) & 0x7u); }
static inline uint8_t f3d_cc_a1(uint32_t w0, uint32_t w1) { (void)w1; return (uint8_t)((w0 >> 5) & 0xFu); }
static inline uint8_t f3d_cc_b1(uint32_t w0, uint32_t w1) { (void)w0; return (uint8_t)((w1 >> 24) & 0xFu); }
static inline uint8_t f3d_cc_c1(uint32_t w0, uint32_t w1) { (void)w1; return (uint8_t)(w0 & 0x1Fu); }
static inline uint8_t f3d_cc_d1(uint32_t w0, uint32_t w1) { (void)w0; return (uint8_t)((w1 >> 6) & 0x7u); }
static inline uint8_t f3d_cc_Aa1(uint32_t w0, uint32_t w1) { (void)w0; return (uint8_t)((w1 >> 21) & 0x7u); }
static inline uint8_t f3d_cc_Ab1(uint32_t w0, uint32_t w1) { (void)w0; return (uint8_t)((w1 >> 3) & 0x7u); }
static inline uint8_t f3d_cc_Ac1(uint32_t w0, uint32_t w1) { (void)w0; return (uint8_t)((w1 >> 18) & 0x7u); }
static inline uint8_t f3d_cc_Ad1(uint32_t w0, uint32_t w1) { (void)w0; return (uint8_t)(w1 & 0x7u); }
/* All 16 inputs: out[cycle][0..7] = a, b, c, d, Aa, Ab, Ac, Ad. */
static inline void f3d_cc_decode(uint32_t w0, uint32_t w1, uint8_t out[2][8])
{
    out[0][0] = f3d_cc_a0(w0, w1);  out[0][1] = f3d_cc_b0(w0, w1);
    out[0][2] = f3d_cc_c0(w0, w1);  out[0][3] = f3d_cc_d0(w0, w1);
    out[0][4] = f3d_cc_Aa0(w0, w1); out[0][5] = f3d_cc_Ab0(w0, w1);
    out[0][6] = f3d_cc_Ac0(w0, w1); out[0][7] = f3d_cc_Ad0(w0, w1);
    out[1][0] = f3d_cc_a1(w0, w1);  out[1][1] = f3d_cc_b1(w0, w1);
    out[1][2] = f3d_cc_c1(w0, w1);  out[1][3] = f3d_cc_d1(w0, w1);
    out[1][4] = f3d_cc_Aa1(w0, w1); out[1][5] = f3d_cc_Ab1(w0, w1);
    out[1][6] = f3d_cc_Ac1(w0, w1); out[1][7] = f3d_cc_Ad1(w0, w1);
}

/* ---- G_SETTILE (gbi.h:3301-3324): w0 = SETTILE<<24 | fmt<<21 | siz<<19 | line<<9 | tmem;
 * w1 = tile<<24 | palette<<20 | cmt<<18 | maskt<<14 | shiftt<<10 | cms<<8 | masks<<4 | shifts. */
struct f3d_settile {
    uint8_t fmt;     /* 21..23 G_IM_FMT_* */
    uint8_t siz;     /* 19..20 G_IM_SIZ_* */
    uint16_t line;   /* 9..17, 64-bit words per row */
    uint16_t tmem;   /* 0..8, 64-bit word address in TMEM */
    uint8_t tile;    /* w1 24..26 */
    uint8_t palette; /* w1 20..23 */
    uint8_t cmt;     /* w1 18..19 */
    uint8_t maskt;   /* w1 14..17 */
    uint8_t shiftt;  /* w1 10..13 */
    uint8_t cms;     /* w1 8..9 */
    uint8_t masks;   /* w1 4..7 */
    uint8_t shifts;  /* w1 0..3 */
};
static inline void f3d_settile_decode(uint32_t w0, uint32_t w1, struct f3d_settile *t)
{
    t->fmt = (uint8_t)((w0 >> 21) & 0x7u);
    t->siz = (uint8_t)((w0 >> 19) & 0x3u);
    t->line = (uint16_t)((w0 >> 9) & 0x1FFu);
    t->tmem = (uint16_t)(w0 & 0x1FFu);
    t->tile = (uint8_t)((w1 >> 24) & 0x7u);
    t->palette = (uint8_t)((w1 >> 20) & 0xFu);
    t->cmt = (uint8_t)((w1 >> 18) & 0x3u);
    t->maskt = (uint8_t)((w1 >> 14) & 0xFu);
    t->shiftt = (uint8_t)((w1 >> 10) & 0xFu);
    t->cms = (uint8_t)((w1 >> 8) & 0x3u);
    t->masks = (uint8_t)((w1 >> 4) & 0xFu);
    t->shifts = (uint8_t)(w1 & 0xFu);
}

/* ---- G_SETTILESIZE (gbi.h:3292) and G_LOADTILE (gbi.h:3296): shared layout.
 * w0 = cmd<<24 | uls<<12 | ult, w1 = tile<<24 | lrs<<12 | lrt.
 * All four are 10.2 fixed point (texel*4). */
static inline void f3d_tilesize_decode(uint32_t w0, uint32_t w1, uint8_t *tile,
                                       uint16_t *uls, uint16_t *ult, uint16_t *lrs, uint16_t *lrt)
{
    *uls = (uint16_t)((w0 >> 12) & 0xFFFu);
    *ult = (uint16_t)(w0 & 0xFFFu);
    *tile = (uint8_t)((w1 >> 24) & 0x7u);
    *lrs = (uint16_t)((w1 >> 12) & 0xFFFu);
    *lrt = (uint16_t)(w1 & 0xFFFu);
}
#define f3d_settilesize_decode f3d_tilesize_decode
#define f3d_loadtile_decode    f3d_tilesize_decode

/* ---- G_LOADBLOCK (gbi.h:3336): w0 = LOADBLOCK<<24 | uls<<12 | ult (integer texels),
 * w1 = tile<<24 | lrs<<12 | dxt. lrs = texel count - 1 (clamped to G_TX_LDBLK_MAX_TXL),
 * dxt is 1.11 fixed (0 = already swizzled). */
static inline void f3d_loadblock_decode(uint32_t w0, uint32_t w1, uint8_t *tile,
                                        uint16_t *uls, uint16_t *ult, uint16_t *lrs, uint16_t *dxt)
{
    *uls = (uint16_t)((w0 >> 12) & 0xFFFu);
    *ult = (uint16_t)(w0 & 0xFFFu);
    *tile = (uint8_t)((w1 >> 24) & 0x7u);
    *lrs = (uint16_t)((w1 >> 12) & 0xFFFu);
    *dxt = (uint16_t)(w1 & 0xFFFu);
}

/* ---- G_LOADTLUT (gbi.h:3356 gDPLoadTLUTCmd): w1 = tile<<24 | count<<14.
 * gDPLoadTLUTCmd stores its count argument unchanged, so the field is whatever
 * the caller passed (libultra callers pass entries-1). Returned raw. */
static inline void f3d_loadtlut_decode(uint32_t w1, uint8_t *tile, uint16_t *count)
{
    *tile = (uint8_t)((w1 >> 24) & 0x7u);
    *count = (uint16_t)((w1 >> 14) & 0x3FFu);
}

/* ---- G_SETTIMG / G_SETCIMG / G_SETZIMG (gbi.h:2997-3024 gSetImage):
 * w0 = cmd<<24 | fmt<<21 | siz<<19 | (width-1), w1 = segmented address.
 * SETZIMG passes fmt=0, siz=0, width=1 (gbi.h:3018), so width decodes to 1. */
static inline void f3d_setimg_decode(uint32_t w0, uint32_t w1, uint8_t *fmt, uint8_t *siz,
                                     uint16_t *width, uint32_t *addr)
{
    *fmt = (uint8_t)((w0 >> 21) & 0x7u);
    *siz = (uint8_t)((w0 >> 19) & 0x3u);
    *width = (uint16_t)((w0 & 0xFFFu) + 1u);
    *addr = w1;
}

/* ---- G_TEXRECT (gbi.h:4442 gDPTextureRectangle, 12-bit fields):
 * w0 = TEXRECT<<24 | xh<<12 | yh, w1 = tile<<24 | xl<<12 | yl.
 * xl/yl = upper-left, xh/yh = lower-right, all 10.2 fixed point.
 * Followed by G_RDPHALF_1 (w1 = s<<16 | t) and G_RDPHALF_2 (w1 = dsdx<<16 | dtdy)
 * from gbi.h:4482-4483 (gsSPTextureRectangle). */
static inline void f3d_texrect_decode(uint32_t w0, uint32_t w1, uint16_t *xl, uint16_t *yl,
                                      uint16_t *xh, uint16_t *yh, uint8_t *tile)
{
    *xh = (uint16_t)((w0 >> 12) & 0xFFFu);
    *yh = (uint16_t)(w0 & 0xFFFu);
    *tile = (uint8_t)((w1 >> 24) & 0x7u);
    *xl = (uint16_t)((w1 >> 12) & 0xFFFu);
    *yl = (uint16_t)(w1 & 0xFFFu);
}
/* RDPHALF_1 word: s is s10.5 (texels = raw/32), t likewise. */
static inline void f3d_texrect_half1_decode(uint32_t w1, int16_t *s, int16_t *t)
{
    *s = (int16_t)(uint16_t)(w1 >> 16);
    *t = (int16_t)(uint16_t)(w1 & 0xFFFFu);
}
/* RDPHALF_2 word: dsdx, dtdy are s5.10 (texels per pixel = raw/1024). Copy mode divides by 4 more. */
static inline void f3d_texrect_half2_decode(uint32_t w1, int16_t *dsdx, int16_t *dtdy)
{
    *dsdx = (int16_t)(uint16_t)(w1 >> 16);
    *dtdy = (int16_t)(uint16_t)(w1 & 0xFFFFu);
}

/* ---- G_FILLRECT (gbi.h:4320 gDPFillRectangle): w0 = FILLRECT<<24 | lrx<<14 | lry<<2,
 * w1 = ulx<<14 | uly<<2. Each coordinate is a 10-bit raw 10.2 value (gbi.h stores it
 * unscaled). Fill and copy modes include the lower-right edge. */
static inline void f3d_fillrect_decode(uint32_t w0, uint32_t w1, uint16_t *lrx, uint16_t *lry,
                                       uint16_t *ulx, uint16_t *uly)
{
    *lrx = (uint16_t)((w0 >> 14) & 0x3FFu);
    *lry = (uint16_t)((w0 >> 2) & 0x3FFu);
    *ulx = (uint16_t)((w1 >> 14) & 0x3FFu);
    *uly = (uint16_t)((w1 >> 2) & 0x3FFu);
}

/* ---- G_SETSCISSOR (gbi.h:4274 gDPSetScissor): w0 = SETSCISSOR<<24 | ulx<<12 | uly,
 * w1 = mode<<24 | lrx<<12 | lry. Coordinates are 10.2 (gDPSetScissor multiplies by 4). */
static inline void f3d_scissor_decode(uint32_t w0, uint32_t w1, uint8_t *mode,
                                      uint16_t *ulx, uint16_t *uly, uint16_t *lrx, uint16_t *lry)
{
    *ulx = (uint16_t)((w0 >> 12) & 0xFFFu);
    *uly = (uint16_t)(w0 & 0xFFFu);
    *mode = (uint8_t)((w1 >> 24) & 0x3u);
    *lrx = (uint16_t)((w1 >> 12) & 0xFFFu);
    *lry = (uint16_t)(w1 & 0xFFFu);
}

/* ---- G_SETPRIMCOLOR (gbi.h:3156): w0 = PRIMCOLOR<<24 | minlevel<<8 | lodfrac,
 * w1 = r<<24 | g<<16 | b<<8 | a. */
static inline void f3d_primcolor_decode(uint32_t w0, uint32_t w1, uint8_t *minlevel, uint8_t *lodfrac,
                                        uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a)
{
    *minlevel = (uint8_t)((w0 >> 8) & 0xFFu);
    *lodfrac = (uint8_t)(w0 & 0xFFu);
    *r = (uint8_t)(w1 >> 24);
    *g = (uint8_t)(w1 >> 16);
    *b = (uint8_t)(w1 >> 8);
    *a = (uint8_t)w1;
}
/* G_SETENVCOLOR / G_SETFOGCOLOR / G_SETBLENDCOLOR (gbi.h:3123 DPRGBColor): w1 = r<<24|g<<16|b<<8|a. */
static inline void f3d_rgba_decode(uint32_t w1, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a)
{
    *r = (uint8_t)(w1 >> 24);
    *g = (uint8_t)(w1 >> 16);
    *b = (uint8_t)(w1 >> 8);
    *a = (uint8_t)w1;
}
/* G_SETFILLCOLOR (gbi.h:3144): w1 is the packed fill word. On a 16-bit colour image
 * that is two RGBA5551 halves; interp.rs takes the high half (w1 >> 16). On the z image
 * the same word is the z value. */
static inline uint16_t f3d_fillcolor_rgba5551(uint32_t w1) { return (uint16_t)(w1 >> 16); }

/* ---- G_SETPRIMDEPTH (gbi.h:3149): w1 = z<<16 | dz. */
static inline void f3d_primdepth_decode(uint32_t w1, uint16_t *z, uint16_t *dz)
{
    *z = (uint16_t)(w1 >> 16);
    *dz = (uint16_t)(w1 & 0xFFFFu);
}

#endif /* WINGS64_GFX_F3D_GBI_H */
