# Binaural Sender

Metadata sender plugin for the Binaural Mix renderer.

The plugin is a pass-through audio effect. Put it on a DAW track, set an
Object ID and track name, and it publishes that metadata for the mixer UI.
It does not send or route audio blocks. Audio routing is handled by the User in the DAW.

Metadata files are written here:

```text
/Users/Shared/BinuaralMix/Senders/sender_<instance>.bus
```

The matching renderer side uses `BinuaralTransport::readAllSenderMetadata()`
from `Source/SharedObjectAudioTransport.h` to read active sender track names
and object IDs.

## Files

- `Source/PluginProcessor.*`: pass-through sender processor and state.
- `Source/PluginEditor.*`: simple native JUCE UI.
- `Source/SharedObjectAudioTransport.h`: shared metadata layout, sender, and reader helper.
- `BinuaralSender.jucer`: JUCE project file.

