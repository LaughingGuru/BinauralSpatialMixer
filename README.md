# Binaural Suite

Binaural Suite contains two JUCE audio plugins:

- `BinuaralMix` - the binaural mixer/renderer plugin.
- `BinuaralSender` - the sender plugin for publishing object ID and track name metadata to the mixer.

## Folder Layout

```text
BinuaralSuite/
  BinauralMix/
  BinauralSender/
  CMakeLists.txt
  CMakePresets.json
```

## Important Shared File

Both plugins include a copy of:

```text
Source/SharedObjectAudioTransport.h
```

Keep this file in sync between `BinuaralMix` and `BinuaralSender` whenever the sender/mixer metadata format changes.

The project and folder names currently still use `Binuaral...` to avoid
breaking the existing JUCE/Xcode setup. The intended product spelling in
documentation is "Binaural".

## CMake Build

The suite can be built without Projucer using CMake. CMake looks for JUCE in this order:

1. `-DJUCE_ROOT=/path/to/JUCE`
2. `external/JUCE`
3. `/Applications/JUCE` on macOS
4. Fetch JUCE from GitHub

Configure and build both plugins:

```bash
cmake -S . -B build -DJUCE_ROOT=/Applications/JUCE
cmake --build build --config Release
```

To generate an Xcode project:

```bash
cmake --preset xcode
cmake --build --preset xcode-release
```

The CMake targets keep the existing binary/plugin names:

```text
BinuaralMix
BinuaralSender
```

This avoids breaking existing JUCE/Xcode project settings and host plugin IDs while the public-facing documentation uses the corrected "Binaural" spelling.

## Projucer Build

Open each `.jucer` file in Projucer and resave the exporter if needed, then build the VST3 target from Xcode.

Plugin binaries such as `.vst3`, `.component`, `.app`, CMake build directories, and Xcode build output are intentionally ignored by Git.
