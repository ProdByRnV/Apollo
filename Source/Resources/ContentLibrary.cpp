#include "Resources/ContentLibrary.h"

#include "ApolloVersion.h"
#include "Parameters/ParameterLayout.h"
#include "Resources/EmbeddedAudio.h"
#include "Resources/NoiseSampleFile.h"
#include "Resources/PresetLibrary.h"
#include "Resources/WavetableFile.h"

#include <algorithm>

namespace apollo::resources
{

juce::String toToken (ContentKind kind)
{
    return kind == ContentKind::noise ? "noise" : "wavetable";
}

bool kindFromToken (const juce::String& token, ContentKind& kind)
{
    if (token == "wavetable")
    {
        kind = ContentKind::wavetable;
        return true;
    }

    if (token == "noise")
    {
        kind = ContentKind::noise;
        return true;
    }

    return false;
}

juce::String wildcardFor (ContentKind kind)
{
    // What JUCE's basic formats read. Ogg is a recording format, not one a
    // wavetable editor writes, so it is offered for samples only.
    return kind == ContentKind::noise ? "*.wav;*.aif;*.aiff;*.flac;*.ogg"
                                      : "*.wav;*.aif;*.aiff;*.flac";
}

ContentLocations defaultContentLocations()
{
    const auto product = params::toJuceString (productName);

    const auto user = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile (product);
    const auto shared = juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory).getChildFile (product);

    ContentLocations locations;
    locations.userWavetables = user.getChildFile ("Wavetables");
    locations.userNoise = user.getChildFile ("Noise");
    locations.factoryWavetables = shared.getChildFile ("Wavetables");
    locations.factoryNoise = shared.getChildFile ("Noise");
    return locations;
}

namespace
{

/** @p file's path below @p root, '/'-separated, or empty if it is not below. */
[[nodiscard]] juce::String relativeTo (const juce::File& file, const juce::File& root)
{
    if (root == juce::File() || ! file.isAChildOf (root))
        return {};

    return file.getRelativePathFrom (root).replaceCharacter ('\\', '/');
}

/** Reads @p entry's header and says what it can of it. */
void describeHeader (juce::AudioFormatManager& formats, ContentEntry& entry)
{
    const auto bytes = entry.file.getSize();
    const auto limit = entry.kind == ContentKind::wavetable ? static_cast<juce::int64> (maximumWavetableBytes)
                                                            : maximumNoiseSampleBytes;

    if (bytes > limit)
    {
        entry.problem = entry.kind == ContentKind::wavetable ? describe (WavetableLoadResult::tooLarge)
                                                             : describe (NoiseSampleLoadResult::tooLarge);
        return;
    }

    const std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (entry.file) };

    if (reader == nullptr || reader->numChannels == 0 || ! (reader->sampleRate > 0.0))
    {
        entry.problem = describe (WavetableLoadResult::unreadable);
        return;
    }

    const auto length = reader->lengthInSamples;

    if (length <= 0)
    {
        entry.problem = describe (WavetableLoadResult::empty);
        return;
    }

    if (entry.kind == ContentKind::wavetable)
    {
        if (length % wavetableFrameSamples != 0)
            entry.problem = describe (WavetableLoadResult::wrongFrameLength);
        else if (length / wavetableFrameSamples > maximumWavetableFrames)
            entry.problem = describe (WavetableLoadResult::tooManyFrames);
        else
            entry.frames = static_cast<int> (length / wavetableFrameSamples);

        return;
    }

    entry.seconds = static_cast<double> (length) / reader->sampleRate;

    if (entry.seconds > maximumNoiseSampleSeconds + 1.0e-9)
        entry.problem = describe (NoiseSampleLoadResult::tooLong);
}

void scanRoot (const juce::File& root, ContentKind kind, bool factory, juce::AudioFormatManager& formats,
               ContentIndex& index, const std::function<bool()>& shouldAbort)
{
    if (! root.isDirectory())
        return;

    // Depth-limited by hand rather than by a recursive iterator, so a symbolic
    // link that points at its own parent ends at the bound rather than never.
    struct Pending
    {
        juce::File folder;
        int depth;
    };

    std::vector<Pending> folders { { root, 0 } };
    const auto extensions = juce::StringArray::fromTokens (wildcardFor (kind).removeCharacters ("*"), ";", {});

    while (! folders.empty())
    {
        const auto [folder, depth] = folders.back();
        folders.pop_back();

        for (const auto& child : folder.findChildFiles (juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles,
                                                        false))
        {
            if (shouldAbort && shouldAbort())
                return;

            if (child.isDirectory())
            {
                if (depth + 1 < maximumContentScanDepth)
                    folders.push_back ({ child, depth + 1 });
                else
                    index.truncated = true;

                continue;
            }

            if (! extensions.contains (child.getFileExtension(), true))
                continue;

            if (static_cast<int> (index.entries.size()) >= maximumContentEntries)
            {
                index.truncated = true;
                return;
            }

            ContentEntry entry;
            entry.kind = kind;
            entry.file = child;
            entry.factory = factory;
            entry.name = child.getFileNameWithoutExtension();
            entry.libraryPath = relativeTo (child, root);
            entry.bank = relativeTo (child.getParentDirectory(), root);

            describeHeader (formats, entry);
            index.entries.push_back (std::move (entry));
        }
    }
}

} // namespace

ContentIndex scanContent (const ContentLocations& locations, const std::function<bool()>& shouldAbort)
{
    ContentIndex index;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    for (const auto kind : { ContentKind::wavetable, ContentKind::noise })
    {
        scanRoot (locations.userRoot (kind), kind, false, formats, index, shouldAbort);
        scanRoot (locations.factoryRoot (kind), kind, true, formats, index, shouldAbort);
    }

    // Wavetables, then samples; within each, the user's own before the
    // factory's; then by bank and name, and the path to make the order total.
    std::sort (index.entries.begin(), index.entries.end(), [] (const ContentEntry& a, const ContentEntry& b)
    {
        if (a.kind != b.kind)
            return a.kind == ContentKind::wavetable;

        if (a.factory != b.factory)
            return ! a.factory;

        if (const auto bank = a.bank.compareNatural (b.bank); bank != 0)
            return bank < 0;

        if (const auto name = a.name.compareNatural (b.name); name != 0)
            return name < 0;

        return a.libraryPath.compareIgnoreCase (b.libraryPath) < 0;
    });

    auto next = 1;

    for (auto& entry : index.entries)
        entry.id = next++;

    return index;
}

const ContentEntry* findContent (const ContentIndex& index, int id)
{
    for (const auto& entry : index.entries)
        if (entry.id == id)
            return &entry;

    return nullptr;
}

const ContentEntry* findContent (const ContentIndex& index, ContentKind kind, const ContentSource& source)
{
    if (source.origin == ContentOrigin::file || source.libraryPath.isEmpty())
        return nullptr;

    const auto factory = source.origin == ContentOrigin::factoryLibrary;

    for (const auto& entry : index.entries)
        if (entry.kind == kind && entry.factory == factory && entry.libraryPath == source.libraryPath)
            return &entry;

    return nullptr;
}

ContentSource sourceFor (const ContentLocations& locations, ContentKind kind, const juce::File& file)
{
    ContentSource source;
    source.name = file.getFileNameWithoutExtension().substring (0, UserContent::maximumNameLength);

    if (const auto path = relativeTo (file, locations.userRoot (kind)); path.isNotEmpty())
    {
        source.origin = ContentOrigin::library;
        source.libraryPath = path;
    }
    else if (const auto factoryPath = relativeTo (file, locations.factoryRoot (kind)); factoryPath.isNotEmpty())
    {
        source.origin = ContentOrigin::factoryLibrary;
        source.libraryPath = factoryPath;
    }

    return source;
}

//==============================================================================

juce::String describe (ContentSaveResult result)
{
    switch (result)
    {
        case ContentSaveResult::ok:                   return "Saved to your library.";
        case ContentSaveResult::nothingToSave:        return "There is nothing loaded there to save.";
        case ContentSaveResult::unreadable:           return "The audio in this patch could not be read back.";
        case ContentSaveResult::couldNotCreateFolder: return "Your library folder could not be created.";
        case ContentSaveResult::couldNotWrite:        return "The file could not be written.";
    }

    return "The file could not be written.";
}

ContentSaveResult saveContent (const juce::String& audio, ContentKind kind, const juce::String& name,
                               const juce::File& directory, juce::File& written)
{
    if (audio.isEmpty())
        return ContentSaveResult::nothingToSave;

    auto reader = decodeAudio (audio);

    if (reader == nullptr || reader->lengthInSamples <= 0
        || reader->lengthInSamples > std::numeric_limits<int>::max())
        return ContentSaveResult::unreadable;

    const auto length = static_cast<int> (reader->lengthInSamples);
    const auto channels = kind == ContentKind::wavetable ? 1 : static_cast<int> (std::min (2u, reader->numChannels));

    juce::AudioBuffer<float> buffer (channels, length);

    if (! reader->read (&buffer, 0, length, 0, true, channels > 1))
        return ContentSaveResult::unreadable;

    if (! directory.isDirectory() && ! directory.createDirectory().wasOk())
        return ContentSaveResult::couldNotCreateFolder;

    auto safeName = toSafeFileName (name);

    if (safeName.isEmpty())
        safeName = kind == ContentKind::wavetable ? "Wavetable" : "Noise";

    // Never over somebody's file: a taken name becomes "Name (2)", written as a
    // person writes it — JUCE's own numbering leaves out the space.
    auto target = directory.getChildFile (safeName + ".wav");

    for (int n = 2; target.exists() && n < 10000; ++n)
        target = directory.getChildFile (safeName + " (" + juce::String (n) + ").wav");

    if (target.exists())
        return ContentSaveResult::couldNotWrite;

    juce::TemporaryFile temporary (target);

    {
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (temporary.getFile());

        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (stream,
                                           juce::AudioFormatWriterOptions {}
                                               .withSampleRate (reader->sampleRate)
                                               .withNumChannels (channels)
                                               .withBitsPerSample (24));

        if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (buffer, 0, length))
            return ContentSaveResult::couldNotWrite;
    }

    if (! temporary.overwriteTargetFileWithTemporary())
        return ContentSaveResult::couldNotWrite;

    written = target;
    return ContentSaveResult::ok;
}

//==============================================================================

ContentLibrary::ContentLibrary()
    : juce::Thread ("Apollo content scan"),
      locations (defaultContentLocations())
{
}

ContentLibrary::~ContentLibrary()
{
    cancelPendingUpdate();
    stopThread (4000);
}

void ContentLibrary::setLocations (const ContentLocations& newLocations)
{
    const juce::ScopedLock guard (lock);
    locations = newLocations;
}

ContentLocations ContentLibrary::getLocations() const
{
    const juce::ScopedLock guard (lock);
    return locations;
}

void ContentLibrary::rescan()
{
    stopThread (4000);
    startThread();
}

ContentIndex ContentLibrary::getIndex() const
{
    const juce::ScopedLock guard (lock);
    return index;
}

bool ContentLibrary::revealFolder (ContentKind kind) const
{
    const auto folder = getLocations().userRoot (kind);

    if (! folder.isDirectory() && ! folder.createDirectory().wasOk())
        return false;

    // Opened, not revealed inside its parent: what somebody wants is to see
    // what is in it and drop files there.
    return folder.startAsProcess();
}

void ContentLibrary::run()
{
    auto found = scanContent (getLocations(), [this] { return threadShouldExit(); });

    if (threadShouldExit())
        return;

    {
        const juce::ScopedLock guard (lock);
        index = std::move (found);
    }

    pendingDelivery.store (true);
    triggerAsyncUpdate();
}

void ContentLibrary::handleAsyncUpdate()
{
    flushPendingNotification();
}

void ContentLibrary::flushPendingNotification()
{
    if (pendingDelivery.exchange (false) && onIndexUpdated)
        onIndexUpdated();
}

void ContentLibrary::waitForScan()
{
    while (isThreadRunning())
        juce::Thread::sleep (1);
}

} // namespace apollo::resources
