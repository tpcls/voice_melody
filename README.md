# Voice Melody

Voice Melody is a JUCE-based DAW plugin that turns a sung or hummed monophonic melody into MIDI notes. Insert it on an audio track, sing into the track, and route the plugin's MIDI output to a software instrument track to sketch melodies quickly.

## Features

- Real-time voice-to-MIDI conversion using a YIN-style pitch detector.
- MIDI note-on/note-off generation with configurable velocity and transpose.
- Gate threshold, confidence, and smoothing controls to reduce false notes.
- VST3, AU, and standalone targets through JUCE/CMake.
- The audio input is muted at the output so the plugin can be used as a clean MIDI generator.

## Build

Requirements:

- CMake 3.22+
- A C++17 compiler
- Platform plugin SDK prerequisites required by JUCE/your DAW

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

By default, configure does not attempt network downloads. If JUCE is not installed, CMake still configures successfully and creates a placeholder target that explains the missing dependency. To build the real plugin, either install JUCE and pass its package path with `CMAKE_PREFIX_PATH`, add JUCE from a parent CMake project, or enable automatic download:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DVOICEMELODY_FETCH_JUCE=ON
```

Built plugins are copied after build when supported by JUCE.

## CLI usage

You can also use Voice Melody without a DAW or plugin host. The dependency-free CLI converts a mono or stereo PCM WAV file into a Standard MIDI File:

```bash
python3 voice_melody_cli.py vocal.wav melody.mid
python3 voice_melody_cli.py vocal.wav melody.mid --show-ui
```

For live microphone capture, install the optional audio input package and run realtime mode. It listens until you press `Ctrl+C`, then writes the detected MIDI notes:

```bash
python3 -m pip install sounddevice
python3 voice_melody_cli.py --realtime live_take.mid --show-ui
```

Useful options:

- `--gate-threshold` adjusts the minimum input level before notes are emitted.
- `--confidence` adjusts how strict the YIN pitch detector should be.
- `--transpose` shifts emitted notes by semitones.
- `--smoothing` requires several consecutive frames before changing notes.
- `--tempo` writes the MIDI file tempo metadata.
- `--show-ui` prints note names and a simple terminal pitch/timeline UI instead of only numeric MIDI output.
- `--realtime` listens to a microphone and writes MIDI when stopped.
- `--device`, `--sample-rate`, `--channels`, and `--block-size` tune realtime audio capture.

The CLI is intended for monophonic singing or humming recordings and works best on clean, dry vocals.

## DAW setup

1. Add **Voice Melody** to an audio track that receives your microphone.
2. Enable monitoring/record-arm for the audio track.
3. Create an instrument track and select **Voice Melody** as the MIDI input/source.
4. Sing or hum one note at a time. Use headphones to avoid bleed.
5. Tune **Gate Threshold**, **Pitch Confidence**, and **Note Smoothing** until MIDI notes feel stable.

## Current limitations

- Designed for monophonic voice or humming, not chords or polyphonic audio.
- Pitch tracking works best with clear vowels and stable notes between roughly 70 Hz and 1000 Hz.
- Different DAWs expose plugin MIDI output differently; consult the DAW's routing docs if no MIDI appears.
