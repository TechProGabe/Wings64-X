# 044-rate-random-draws

**Why (PLAN.md M5):** four places draw a random number every frame; the result per second depends on how
many frames there are:

- `app/environment.c` `env_802E15F0`: the wind's speed and direction take a random step
  `(rand - 0.5) * rate * dt` a frame. A random walk spreads as the square root of the step count, so the
  wind varied 0.71x as much at 60 fps as at the N64's 30 (it feeds all flight physics).
- `app/code_BA190.c` `func_80332FCC` (the player skydiver) and `app/skydiving.c` `func_803322CC` (the
  formation divers): a new random drift target per frame, low-passed (`func_80313AF4`, k = 10): the drift
  jitter goes as the square root of `dt`.
- `app/birdman1.c` `bird_802CE190`: the wing flutter factor, the same way (k = 15).

**What (`DIVERGES`):** the wind's steps are multiplied by `wx_rate_walk()` = sqrt(N64 frames per frame)
(the N64 frame's spread per second). The drift and flutter targets are drawn once per N64 frame
(`wx_rate_ticks`) and held in between (the player's per vehicle-data pointer, the formation's per
`D_80371970` entry); the flutter's two discarded draws are kept, so at 30 fps every frame draws exactly
what the N64 drew, in its order.

**Kill switch:** `-DWX_RATE=0` (walk factor 1, a draw every frame).

**Origin:** Birdman64 (MIT), `environment.c.patch`, `code_BA190.c.patch`, `skydiving.c.patch`,
`birdman1.c.patch` (their split-screen state not taken).

**N64 build:** unchanged (`#ifdef WINGS64_XBOX` / `#else` original).
