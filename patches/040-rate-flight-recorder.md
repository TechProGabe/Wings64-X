# 040-rate-flight-recorder

**Why (PLAN.md M5):** the flight recorder (`app/fdr.c`, the results screen's replay) samples the pose,
sticks and vehicle values every 2nd frame (`fdr_802E65AC`, `D_8035AF60 >= 2`) into 200-entry rings, so a
replay holds the last 400 frames: ~13 s at the N64's 30 fps, 6.7 s at 60. `fdr_802E66DC` (the crash
paths of the hang glider, rocket belt, gyrocopter and birdman) forces every-frame samples.

**What (`DIVERGES`):** a sample is due when 2 N64 frames (`2 * WX_REF_DT`; forced: 1) of recorder time
(`D_8035AF68`, which already advances by `dt`) have passed since the last one (`sWxFdrSampleTime`, reset to
"long ago" when recording starts, so the first frame samples). `fdr_802E66DC` flags the vehicle samples
(`fdr_802E682C`) only when an N64 frame has passed. Playback interpolates by timestamp
(`fdrInterp`), so it plays back the same at any rate.

**Kill switch:** `-DWX_RATE=0` (the frame count).

**Origin:** Birdman64 (MIT), `fdr.c.patch` (its split-screen recorder array not taken).

**N64 build:** unchanged (`#ifdef WINGS64_XBOX` / `#else` original).
