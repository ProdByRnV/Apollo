#pragma once

/*
    The tables the instrument can play, and how one is replaced underneath it.

    WHAT IS BUILT IN. Four tables, each sixteen frames, described as spectra and
    rendered at construction (WavetableBuilder.h). They are always present, they
    cannot be lost, and they are what every slot starts and falls back to.

    WHAT CAN REPLACE ONE. A table read from a file is analysed, band-limited and
    then *published* into a slot. Nothing about playing it differs from playing
    a built-in: by the time it reaches a voice it is the same kind of object,
    built the same way.

    SWAPPING A TABLE WHILE A NOTE IS SOUNDING is the hard part, and the reason
    this class is more than an array.

    Voices hold a plain pointer to a table for the length of a block. The engine
    asks for that pointer once per block and hands it to every voice, so a swap
    is safe *if* the pointer the audio thread reads is exchanged atomically and
    the table it used to point at outlives the block that is still using it.
    Both are arranged here:

      - each slot is an `atomic<const Wavetable*>`, so the audio thread reads a
        whole pointer or the previous one, never a torn value;
      - a replaced table is not destroyed. It is retired, and freed only once
        the audio thread has begun two blocks since — by which point the block
        that might have been holding it has certainly finished.

    Two blocks rather than one because the swap can land in the middle of a
    block that has already read the old pointer. Counting blocks rather than
    taking a lock is what keeps the audio thread free of the loader entirely:
    it publishes a counter and never waits for anything.

    THE AUDIO THREAD NEVER ALLOCATES OR FREES HERE. `getTable` is an atomic
    load. `beginBlock` is an increment. Everything that allocates — building a
    table, retiring one, freeing one — happens on the thread that loaded it.
*/

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "DSP/Oscillators/Wavetable.h"

namespace apollo::dsp
{

class WavetableLibrary
{
public:
    /** Number of built-in tables: the `oscN_wavetable` parameter's choices
        0 to 3 (UI_BINDINGS.md §3).
    */
    static constexpr int numTables = 4;

    /** The `oscN_wavetable` choice that plays the oscillator's own user table
        rather than a built-in one (Phase 13g, ADR-0104). Appended after the
        built-ins so that every patch made before it keeps its table.
    */
    static constexpr int userChoice = numTables;

    /** How many choices `oscN_wavetable` has. */
    static constexpr int numChoices = numTables + 1;

    /** One user slot per oscillator, so loading a table into one oscillator
        never changes what the other plays. They sit after the built-ins, so the
        slot indices 0 to 3 mean what they always meant.
    */
    static constexpr int numUserSlots = 2;
    static constexpr int numSlots = numTables + numUserSlots;

    /** @returns oscillator @p oscillator's (1 or 2) user slot. */
    [[nodiscard]] static constexpr int userSlot (int oscillator) noexcept
    {
        return numTables + (oscillator == 2 ? 1 : 0);
    }

    /** @returns the slot oscillator @p oscillator plays for @p choice, a value
        of its `oscN_wavetable` parameter: the built-in of that number, or its
        own user slot. Out-of-range choices clamp, never to the other
        oscillator's table.
    */
    [[nodiscard]] static constexpr int slotForChoice (int oscillator, int choice) noexcept
    {
        if (choice >= userChoice)
            return userSlot (oscillator);

        return choice < 0 ? 0 : choice;
    }

    /** The built-in an empty user slot plays: the first, so a USER choice with
        nothing loaded, or with content that failed to load, still sounds
        (CLAUDE.md §33).
    */
    static constexpr int userFallbackTable = 0;

    /** Frames per built-in table.

        The format supports up to 256 (PRD §8.2); the built-ins use 16 because
        that is enough to scan audibly and keeps both memory and the one-off
        generation cost small.
    */
    static constexpr int framesPerTable = 16;

    /** Frames in the sub-oscillator table.

        One. The sub is a pure sine, so there is no second shape to scan
        towards, and a single-harmonic table is correct at every pitch without
        needing its mipmap consulted for anything but interpolation quality.

        A sine sub is a deliberate minimum, not an oversight: it adds weight at
        the fundamental without competing spectrally with the primary
        oscillators, and it cannot alias at any note or octave transposition.
    */
    static constexpr int subTableFrames = 1;

    /** The sub oscillator's waveforms, in the order of the `sub_waveform`
        parameter: sine, triangle, square (PRD §9). Sine is first because it is
        what the sub was before the choice existed, so every patch made before
        it is unchanged.

        All three are built from their spectra like every other table, so the
        triangle and the square are band-limited at every pitch for nothing.
        They are peak-normalised to the sine and start at the same rising zero
        crossing, so changing the waveform does not change the level of a peak
        or click the start of a note.
    */
    static constexpr int numSubWaveforms = 3;

    /** How many blocks a retired table is kept alive before it is freed.

        Two. A swap can land in the middle of a block that has already taken the
        old pointer, so one block is not enough; two is, and anything more is
        memory held for no reason.
    */
    static constexpr std::uint64_t retirementBlocks = 2;

    /** Builds every built-in table. Allocates; never call from the audio
        thread.
    */
    WavetableLibrary();
    ~WavetableLibrary();

    /** @returns the table in slot @p index (0 to numSlots - 1), clamped into
        range.

        AUDIO THREAD. An atomic load and nothing else.

        Never returns a table that is not there: an out-of-range selection
        yields a real table rather than silence, so a bad parameter value
        cannot mute the instrument.
    */
    [[nodiscard]] const Wavetable& getTable (int index) const noexcept;

    /** @returns the sub oscillator's table for @p waveform, clamped into
        [0, numSubWaveforms).

        Separate from the indexed set on purpose: `numTables` is the range of
        the `oscN_wavetable` parameter, and the sub must not appear as a
        selectable primary waveform. Its tables are never replaced, so they need
        none of the machinery above.
    */
    [[nodiscard]] const Wavetable& getSubTable (int waveform = 0) const noexcept;

    /** Marks the start of an audio block.

        AUDIO THREAD. One relaxed increment. This is what tells the loader that
        a block boundary has passed and a retired table is closer to being safe
        to free; without it nothing is ever collected, which is inconvenient
        rather than unsafe.
    */
    void beginBlock() noexcept
    {
        blockCounter.fetch_add (1, std::memory_order_relaxed);
    }

    /** @returns true once after a slot has been published to or restored.

        AUDIO THREAD, and the reason a swap takes effect at all.

        The engine hands each voice a table pointer and the voices keep it until
        they are handed another. It only hands them one when a parameter
        changes, which a wavetable being replaced is not — so without this a
        published table would sit in its slot, played by nobody, until somebody
        happened to move an unrelated control. The same staleness is what would
        make retirement unsafe: a voice could hold a retired pointer for as long
        as nothing else changed.

        So the engine asks this at the top of every block and, when the answer
        is yes, re-hands the pointers before it renders a sample.
    */
    [[nodiscard]] bool takeGenerationChange() noexcept
    {
        const auto latest = generation.load (std::memory_order_acquire);

        if (latest == seenByAudio)
            return false;

        seenByAudio = latest;
        return true;
    }

    /** Puts @p table into slot @p index, retiring whatever was there.

        MESSAGE THREAD. Not merely "not the audio thread": this and
        `collectRetired` both touch the retired list, so they have to be the
        same thread as each other. A loader builds its table on a worker and
        hands it over here.

        @returns false, having done nothing, if the index is out of range or the
                 table is empty. An empty table would silence the oscillator,
                 which is never what a caller meant.
    */
    bool publish (int index, std::unique_ptr<Wavetable> table);

    /** Puts slot @p index back to the table Apollo was built with — for a user
        slot, `userFallbackTable`.

        The fallback. A file that has gone missing, or turned out not to be a
        wavetable, leaves the instrument playing something rather than nothing
        (CLAUDE.md §33).
    */
    void restoreBuiltIn (int index);

    /** @returns true if slot @p index is playing something other than its
        built-in table.
    */
    [[nodiscard]] bool isReplaced (int index) const noexcept;

    /** Frees retired tables the audio thread can no longer be holding.

        MESSAGE THREAD. Cheap and safe to call as often as convenient; it frees
        nothing until enough blocks have passed.

        @returns how many were freed.
    */
    int collectRetired();

    /** @returns how many retired tables are still waiting to be freed. */
    [[nodiscard]] int getRetiredCount() const;

private:
    [[nodiscard]] static int clampIndex (int index) noexcept;

    /** A table taken out of service, and the block it left at. */
    struct Retired
    {
        std::unique_ptr<Wavetable> table;
        std::uint64_t retiredAtBlock = 0;
    };

    /** Hands @p previous to the retired list, if there was one. */
    void retire (std::unique_ptr<Wavetable> previous);

    /** What each slot falls back to: the four tables Apollo was built with,
        then `userFallbackTable` again for each user slot. Never replaced, so
        every slot can fall back at any moment.

        Shared rather than owned, because they are identical in every instance
        of the plugin and building them costs real time. They are immutable from
        the moment they exist, so sharing needs no synchronisation.
    */
    std::array<std::shared_ptr<const Wavetable>, static_cast<std::size_t> (numSlots)> builtIn;

    /** The loaded table each slot is playing, or null where the slot is playing
        its built-in. This is what *owns* a published table; `current` only
        points at it.
    */
    std::array<std::unique_ptr<Wavetable>, static_cast<std::size_t> (numSlots)> loaded;

    /** What each slot is currently playing. Read by the audio thread. */
    std::array<std::atomic<const Wavetable*>, static_cast<std::size_t> (numSlots)> current;

    /** Tables the audio thread may still be reading. Message thread only. */
    std::vector<Retired> retired;

    std::array<std::shared_ptr<const Wavetable>, static_cast<std::size_t> (numSubWaveforms)> subTables;

    std::atomic<std::uint64_t> blockCounter { 0 };

    /** Bumped whenever a slot changes what it points at. */
    std::atomic<std::uint32_t> generation { 0 };

    /** The generation the audio thread has acted on. Audio thread only, so a
        plain member rather than an atomic.
    */
    std::uint32_t seenByAudio = 0;
};

} // namespace apollo::dsp
