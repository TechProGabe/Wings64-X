# 022-audio-task-off-scheduler

**Why:** on the N64 the audio thread (priority 110) submits each frame's audio command list to the
scheduler, which hands it to the RSP and returns at once; the RSP's done interrupt wakes the audio thread
before the next retrace. In a port the scheduler thread interprets display lists on the CPU (~30 ms per
flight frame on the Dreamcast), so an audio task queued there waits behind gfx work and the audio thread,
blocked on its reply, misses retraces: wings64-dc's emulator gate measured 7% of audio frames lost and 18%
virtual-DAC underruns (music time dropped, ~20.4 kHz effective).

**What (Xbox):** `__amHandleFrameMsg` runs the frame's audio command list itself, on the audio thread,
through `wings64_audio_task(&task->list)` (the RSP audio HLE, PLAN.md D6, M6), then posts the task's done
message to `audioReplyMsgQ` instead of queuing the task on the scheduler (`DIVERGES`: no audio RSP task;
the work happens on the thread that asked for it). Until M6 `wings64_audio_task` does nothing
(`xbox/src/game/ultra/hooks.c`): silence.

**What (wings64-dc, for the record):** with its M4 the AICA played the voices, so the command list was a
placeholder and the done message was posted without running anything. After it: 300 audio frames per
5 s, 22.0-22.3 kHz, 0 underruns over the gate. Raising the audio thread above the scheduler (priority 200)
was tried there and changed nothing measurable; the N64's 110 is kept.

**Origin:** wings64-dc (M4 audio, its docs/audio-design.md section 6); changed for the Xbox (below).

**N64 build:** unchanged (`#ifdef WINGS64_XBOX` / `#else` original).

**Import:** wings64-dc `0c384bd` `patches/022-audio-task-off-scheduler.patch`, guard `WINGS64_DC` renamed
`WINGS64_XBOX` (one `sed`). **Changed for the Xbox:** the hunk now calls `wings64_audio_task(&task->list)`
before posting the reply (wings64-dc posted it without running the list, because its AICA played the
voices). The Xbox keeps libultra's synthesizer, so the command list must run.
