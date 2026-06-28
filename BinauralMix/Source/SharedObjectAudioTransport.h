#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <cstring>

namespace BinuaralTransport
{
constexpr int maxObjects = 16;
constexpr int maxTrackNameBytes = 64;
constexpr int maxInstanceIdBytes = 64;
constexpr uint32_t magic = 0x424E534D; // BNSM

struct alignas (64) SharedBlock
{
    std::atomic<uint32_t> sequence { 0 };
    uint32_t magicNumber = magic;
    int32_t objectId = 1;
    char instanceId[maxInstanceIdBytes] {};
    char trackName[maxTrackNameBytes] {};
};

inline juce::File getSenderMetadataDirectory()
{
    return juce::File ("/Users/Shared")
        .getChildFile ("BinuaralMix")
        .getChildFile ("Senders");
}

inline juce::String sanitiseInstanceId (const juce::String& instanceId)
{
    juce::String safeId;

    for (auto character : instanceId)
    {
        if (juce::CharacterFunctions::isLetterOrDigit (character)
            || character == '-' || character == '_')
            safeId << character;
    }

    return safeId.isNotEmpty() ? safeId : juce::Uuid().toString();
}

inline juce::File getSenderMetadataFile (const juce::String& instanceId)
{
    return getSenderMetadataDirectory()
        .getChildFile ("sender_" + sanitiseInstanceId (instanceId) + ".bus");
}

inline juce::File getTransportFile (int objectId)
{
    const auto safeObjectId = juce::jlimit (1, maxObjects, objectId);
    return juce::File ("/Users/Shared")
        .getChildFile ("BinuaralMix")
        .getChildFile ("object_" + juce::String (safeObjectId) + ".bus");
}

class Sender
{
public:
    ~Sender()
    {
        if (instanceId.isNotEmpty())
            getSenderMetadataFile (instanceId).deleteFile();
    }

    void prepare (const juce::String& newInstanceId, int objectId)
    {
        setInstanceId (newInstanceId);
        setObjectId (objectId);
    }

    void setInstanceId (const juce::String& newInstanceId)
    {
        const auto safeId = sanitiseInstanceId (newInstanceId);

        if (safeId == instanceId && block != nullptr)
            return;

        instanceId = safeId;
        mappedFile.reset();
        block = nullptr;
        openMetadataFile();
    }

    void setObjectId (int newObjectId)
    {
        const auto clampedId = juce::jlimit (1, maxObjects, newObjectId);

        objectId = clampedId;

        if (block == nullptr)
            openMetadataFile();
    }

    void openMetadataFile()
    {
        if (instanceId.isEmpty())
            instanceId = juce::Uuid().toString();

        const auto file = getSenderMetadataFile (instanceId);
        file.getParentDirectory().createDirectory();

        if (! file.existsAsFile() || file.getSize() != static_cast<juce::int64> (sizeof (SharedBlock)))
        {
            file.deleteFile();
            juce::FileOutputStream stream (file);
            stream.setPosition (static_cast<juce::int64> (sizeof (SharedBlock)) - 1);
            stream.writeByte (0);
        }

        mappedFile = std::make_unique<juce::MemoryMappedFile> (
            file,
            juce::MemoryMappedFile::readWrite,
            false);

        block = static_cast<SharedBlock*> (mappedFile->getData());

        if (block != nullptr && block->magicNumber != magic)
            new (block) SharedBlock();
    }

    void setTrackName (const juce::String& name)
    {
        trackName = name.substring (0, maxTrackNameBytes - 1);
    }

    void writeMetadata()
    {
        if (block == nullptr)
            return;

        const auto nextSequence = block->sequence.load (std::memory_order_relaxed) + 1;

        block->sequence.store (nextSequence | 1u, std::memory_order_release);
        block->magicNumber = magic;
        block->objectId = objectId;

        std::memset (block->instanceId, 0, sizeof (block->instanceId));
        std::memcpy (block->instanceId,
                     instanceId.toRawUTF8(),
                     static_cast<size_t> (juce::jmin<int> (
                         maxInstanceIdBytes - 1,
                         static_cast<int> (instanceId.getNumBytesAsUTF8()))));

        std::memset (block->trackName, 0, sizeof (block->trackName));
        std::memcpy (block->trackName,
                     trackName.toRawUTF8(),
                     static_cast<size_t> (juce::jmin<int> (
                         maxTrackNameBytes - 1,
                         static_cast<int> (trackName.getNumBytesAsUTF8()))));

        block->sequence.store ((nextSequence + 1u) & ~1u, std::memory_order_release);
    }

private:
    int objectId = 1;
    juce::String instanceId;
    juce::String trackName { "Track" };
    std::unique_ptr<juce::MemoryMappedFile> mappedFile;
    SharedBlock* block = nullptr;
};

struct ReceivedBlock
{
    int objectId = 1;
    juce::String instanceId;
    juce::String trackName;
};

class Receiver
{
public:
    bool open (int objectId)
    {
        mappedFile.reset();
        block = nullptr;

        const auto file = getTransportFile (objectId);

        if (! file.existsAsFile())
            return false;

        mappedFile = std::make_unique<juce::MemoryMappedFile> (
            file,
            juce::MemoryMappedFile::readOnly,
            false);

        block = static_cast<const SharedBlock*> (mappedFile->getData());
        return block != nullptr && block->magicNumber == magic;
    }

    bool readLatest (ReceivedBlock& destination)
    {
        if (block == nullptr || block->magicNumber != magic)
            return false;

        const auto sequenceBefore = block->sequence.load (std::memory_order_acquire);

        if ((sequenceBefore & 1u) != 0u)
            return false;

        destination.objectId = block->objectId;
        destination.instanceId = juce::String::fromUTF8 (block->instanceId, maxInstanceIdBytes);
        destination.trackName = juce::String::fromUTF8 (block->trackName, maxTrackNameBytes);

        const auto sequenceAfter = block->sequence.load (std::memory_order_acquire);
        return sequenceBefore == sequenceAfter && (sequenceAfter & 1u) == 0u;
    }

private:
    std::unique_ptr<juce::MemoryMappedFile> mappedFile;
    const SharedBlock* block = nullptr;
};

inline juce::Array<ReceivedBlock> readAllSenderMetadata()
{
    juce::Array<ReceivedBlock> results;
    const auto directory = getSenderMetadataDirectory();

    if (! directory.isDirectory())
        return results;

    juce::Array<juce::File> files;
    directory.findChildFiles (files, juce::File::findFiles, false, "*.bus");

    for (const auto& file : files)
    {
        if (file.getSize() != static_cast<juce::int64> (sizeof (SharedBlock)))
            continue;

        juce::MemoryMappedFile mappedFile (file, juce::MemoryMappedFile::readOnly, false);
        const auto* block = static_cast<const SharedBlock*> (mappedFile.getData());

        if (block == nullptr || block->magicNumber != magic)
            continue;

        const auto sequenceBefore = block->sequence.load (std::memory_order_acquire);

        if ((sequenceBefore & 1u) != 0u)
            continue;

        ReceivedBlock metadata;
        metadata.objectId = juce::jlimit (1, maxObjects, static_cast<int> (block->objectId));
        metadata.instanceId = juce::String::fromUTF8 (block->instanceId, maxInstanceIdBytes);
        metadata.trackName = juce::String::fromUTF8 (block->trackName, maxTrackNameBytes).trim();

        const auto sequenceAfter = block->sequence.load (std::memory_order_acquire);

        if (sequenceBefore == sequenceAfter && (sequenceAfter & 1u) == 0u)
            results.add (metadata);
    }

    struct MetadataComparator
    {
        int compareElements (const ReceivedBlock& a, const ReceivedBlock& b) const
        {
            if (a.objectId != b.objectId)
                return a.objectId < b.objectId ? -1 : 1;

            return a.trackName.compareNatural (b.trackName);
        }
    };

    MetadataComparator comparator;
    results.sort (comparator);

    return results;
}
}
