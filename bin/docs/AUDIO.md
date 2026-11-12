# Z+ Audio

`zaudio` provides audio playback through [miniaudio](https://github.com/mackron/miniaudio) 0.11.26. It is independent of ZGraphics. Interpreter and native Windows x64 execution have been validated; the Linux x86-64 native path is implemented but still requires execution on Linux.

## Initialization

```zp
use zaudio

zaudio.initialize()
if zaudio.initialized() {
    say("Audio is ready")
} end if
zaudio.shutdown()
```

`initialize()` opens the default playback device and is safe to call more than once. It reports an error if no playback device is available or the engine cannot start. `shutdown()` stops playback and releases loaded resources and device state; it is safe to call repeatedly. The runtime also shuts the engine down at process exit.

## Loading and playback

```zp
use zaudio

zaudio.initialize()
let music = zaudio.loadMusic("assets/theme.ogg")
let jump = zaudio.loadSound("assets/jump.wav")

zaudio.setLooping(music, true)
zaudio.setVolume(music, 0.6)
zaudio.play(music)

zaudio.play(jump)
zaudio.pause(music)
zaudio.resume(music)
zaudio.stop(music)

zaudio.unload(jump)
zaudio.unload(music)
zaudio.shutdown()
```

Music is streamed for long files. Sound effects are decoded when loaded and can overlap: each `play()` starts another instance, up to 64 simultaneous instances per sound handle. A sound's volume and looping settings apply to its instances; stopping or unloading one handle does not stop unrelated music or sounds.

Supported file extensions are `.wav`, `.ogg` (Vorbis), and `.mp3`. The decoder validates the file contents, so an extension alone does not make an invalid or unsupported file load successfully.

## Public API

| Function | Description |
|---|---|
| `initialize()` | Start the engine using the selected or default playback device. |
| `initialized() -> boolean` | Report whether the engine is running. |
| `shutdown()` | Stop playback and release all audio resources and device state. |
| `loadMusic(path) -> number` | Load a streamed music resource. |
| `loadSound(path) -> number` | Load a sound-effect resource for overlapping playback. |
| `play(handle)` | Start music or a new sound-effect instance. |
| `pause(handle)` / `resume(handle)` / `stop(handle)` | Pause, resume, or stop playback for a handle. |
| `isPlaying(handle) -> boolean` | Report whether any playback instance for the handle is active. |
| `isPaused(handle) -> boolean` | Report whether playback for the handle is paused. |
| `isStopped(handle) -> boolean` | Report whether the handle has no playing or paused instance. |
| `setVolume(handle, volume)` / `getVolume(handle) -> number` | Set or retrieve per-resource volume. |
| `setLooping(handle, looping)` / `isLooping(handle) -> boolean` | Configure or query looping. |
| `setPlaybackPosition(handle, seconds)` / `getPlaybackPosition(handle) -> number` | Seek or query the current position in seconds. |
| `getDuration(handle) -> number` | Query the decoded duration in seconds. |
| `setMasterVolume(volume)` / `getMasterVolume() -> number` | Set or query engine-wide volume. |
| `mute()` / `unmute()` / `isMuted() -> boolean` | Mute or restore output without changing individual volumes. |
| `deviceCount() -> number` | Return the number of enumerated playback devices. |
| `deviceName(index) -> string` | Return the name at a zero-based device index. |
| `defaultDevice() -> string` | Return the current default device name. |
| `selectDevice(name)` | Select an enumerated playback device. |
| `unload(handle)` | Stop and release one resource; invalidate its handle. |
| `unloadAll()` | Stop and release every loaded resource. |

Volume is clamped to `[0.0, 1.0]`; non-finite values are errors. Seeking requires a finite, non-negative position no greater than the resource duration. Music supports seeking. Invalid, unloaded, or stale handles are errors. Audio operations before initialization initialize the engine when appropriate; loading or playback still fails with an explicit error if no usable device is available.

## Handles and lifetime

Handles are opaque numeric IDs, unique for the process lifetime and checked on every operation. They are not ZMemory-managed objects and are not reclaimed by garbage collection. Call `unload()` or `unloadAll()` when done. `shutdown()` also releases everything.

Unloading an active handle is safe: playback is stopped and the engine's sound objects are uninitialized before their storage is released. The engine serializes application-side operations; miniaudio mixes and streams audio on its own device thread. No Z+ code runs in the audio callback.

## Devices and platform support

`deviceCount()`, `deviceName()`, and `defaultDevice()` enumerate output devices. Call `selectDevice(name)` before loading resources to choose a device. Selecting a device while resources are loaded is rejected; after selection, initialize or continue using the selected device. A device switch cannot be made while playback resources remain loaded.

Device hotplug recovery is not currently implemented. If the selected/default device disconnects, applications should stop, unload, shut down, and initialize again after the device becomes available. Device enumeration and playback require a host audio device; headless API tests do not establish hardware playback.

The current ELF writer uses `/lib64/ld-linux-x86-64.so.2`; Linux distributions with a different dynamic-loader path may require a compiler/runtime adjustment.

| Execution mode | Status |
|---|---|
| Interpreter | Audio API is bound to the native runtime in the current process. |
| Native Windows x64 | `zaudio_runtime.dll` is staged beside audio-enabled executables. |
| Native Linux x86-64 | ELF imports, `libzaudio_runtime.so` staging, and `$ORIGIN` lookup are implemented; execution has not yet been validated on Linux. |
| ZBuild | Native audio imports are detected and the matching runtime is staged and packaged automatically; Linux behavior requires Linux validation. |

Native Z+ numbers are currently integer-oriented. Fractional audio arguments are passed through the native audio ABI, but fractional return values such as duration and playback position are not yet usable as ordinary Z+ floating-point values in native programs. The interpreter returns these values normally. This limitation is independent of audio decoding and must be considered when writing native programs that need numeric audio queries.

## Errors

Missing or unreadable files, unsupported formats, corrupt decoder data, unavailable devices, invalid device indices/names, invalid handles, invalid seek positions, and playback/decoder initialization failures produce descriptive runtime errors. Interpreter programs report these through the standard runtime error path. Native runtime errors are written to standard error and terminate the application because exceptions cannot cross the native C ABI.

## Examples and tests

Runnable examples are in [`test_program/audio/`](../test_program/audio/):

- `music_player.zp` demonstrates streaming, state queries, looping, volume, pause/resume, and seeking-related queries.
- `sound_effects.zp` demonstrates overlapping effects with independent volume.
- `audio_controls.zp` demonstrates device enumeration/selection, master volume, and mute.
- `audio_stress.zp` repeatedly loads, plays, and unloads a sound.

The audio tests are in [`tests/audio/`](../tests/audio/). The headless API test validates decoding, handles, lifecycle, and state without requiring an output device. Interpreter and native integration tests are separate and report when device-dependent playback is skipped.
