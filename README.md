# Binaural Suite

Binaural Suite contains two JUCE audio plugins:

- `BinuaralMix` - the binaural mixer/renderer plugin.
- `BinuaralSender` - the sender plugin for publishing object ID and track name metadata to the mixer.

## Folder Layout

```text
BinuaralSuite/
  BinuaralMix/
  BinuaralSender/
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

## Build Notes

Open each `.jucer` file in Projucer and resave the exporter if needed, then build the VST3 target from Xcode.

Plugin binaries such as `.vst3`, `.component`, `.app`, and Xcode build output are intentionally ignored by Git.
