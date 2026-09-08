#include "Engine/Voice.h"

#include <cmath>

namespace apollo::engine
{

namespace
{

/** Concert-pitch reference. A4 = MIDI note 69 = 440 Hz.

    Fixed for now; a global tuning parameter is a product decision, not a
    Phase 3 one.
*/
constexpr double referenceFrequency = 440.0;
constexpr double referenceNote = 69.0;

/** @returns the frequency of a (possibly fractional) MIDI note number. */
[[nodiscard]] double midiNoteToFrequency (double midiNote) noexcept
{
    return referenceFrequency * std::pow (2.0, (midiNote - referenceNote) / 12.0);
}

/** @returns a positive ramp increment covering the full 0-1 range in @p seconds.

    Guards against a zero or negative sample rate so a mis-prepared voice
    degrades to "instant" rather than producing infinity or NaN, which would
    propagate through the whole mix (CLAUDE.md §34.2).
*/
[[nodiscard]] double rampIncrement (double seconds, double sampleRate) noexcept
{
    const double samples = seconds * sampleRate;
    return samples > 1.0 ? 1.0 / samples : 1.0;
}

/** @returns @p value clamped to [@p low, @p high], mapping NaN to @p low.

    Written as a positive test rather than as `value < low` so that NaN, which
    compares false against everything, falls through to the safe end instead of
    passing straight into a gain (CLAUDE.md §34.2).
*/
[[nodiscard]] float clampFinite (float value, float low, float high) noexcept
{
    if (! (value >= low))
        return low;

    return value > high ? high : value;
}

} // namespace

//==============================================================================

void Voice::SourceGain::reset (double newSampleRate) noexcept
{
    left.reset (newSampleRate, Voice::gainRampSeconds);
    right.reset (newSampleRate, Voice::gainRampSeconds);
}

void Voice::SourceGain::setTargets (float level, float pan) noexcept
{
    const auto clampedLevel = clampFinite (level, 0.0f, 1.0f);
    const auto clampedPan = clampFinite (pan, -1.0f, 1.0f);

    // A balance, not a pan: the signal reaching this gain is already stereo,
    // built by the unison spread. Attenuating the far channel and leaving the
    // near one alone moves the image without re-panning it, and — unlike a
    // constant-power law — no channel gain ever exceeds unity, so no balance
    // setting can push a voice past the level the engine gain staging assumes.
    const auto balanceLeft = clampedPan > 0.0f ? 1.0f - clampedPan : 1.0f;
    const auto balanceRight = clampedPan < 0.0f ? 1.0f + clampedPan : 1.0f;

    left.setTargetValue (clampedLevel * balanceLeft);
    right.setTargetValue (clampedLevel * balanceRight);
}

void Voice::SourceGain::snap() noexcept
{
    left.setCurrentAndTargetValue (left.getTargetValue());
    right.setCurrentAndTargetValue (right.getTargetValue());
}

//==============================================================================

void Voice::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;

    oscillator1.setSampleRate (sampleRate);
    oscillator2.setSampleRate (sampleRate);
    subOscillator.setSampleRate (sampleRate);

    gain1.reset (sampleRate);
    gain2.reset (sampleRate);
    subGain.reset (sampleRate);
    noiseGain.reset (sampleRate);

    amplitudeEnvelope.prepare (sampleRate);

    stealDecrement = static_cast<float> (rampIncrement (stealSeconds, sampleRate));

    reset();
}

void Voice::setStartPhase (double newStartPhase) noexcept
{
    startPhase = newStartPhase - std::floor (newStartPhase);
}

void Voice::setNoiseSeed (std::uint32_t seed) noexcept
{
    noiseSeed = seed;
    noise.setSeed (seed);
}

void Voice::setAmplitudeEnvelope (const dsp::EnvelopeSettings& newSettings) noexcept
{
    amplitudeEnvelope.setSettings (newSettings);
}

void Voice::setFilters (const VoiceFilterSettings& newFilters) noexcept
{
    if (newFilters == filters)
        return;

    // A mode change reroutes which state variables reach the output, but the
    // integrators themselves stay: clearing them on every parameter move would
    // turn a mode switch into a click, and the state is valid for any mode.
    filters = newFilters;

    filter1Left.setMode (filters.filter1.mode);
    filter1Right.setMode (filters.filter1.mode);
    filter1Left.setCoefficients (filters.filter1.coefficients);
    filter1Right.setCoefficients (filters.filter1.coefficients);

    filter2Left.setMode (filters.filter2.mode);
    filter2Right.setMode (filters.filter2.mode);
    filter2Left.setCoefficients (filters.filter2.coefficients);
    filter2Right.setCoefficients (filters.filter2.coefficients);
}

void Voice::applyFilters (float& left, float& right) noexcept
{
    const auto runSlot = [] (const FilterSlotSettings& slot,
                             dsp::StateVariableFilter& leftFilter,
                             dsp::StateVariableFilter& rightFilter,
                             float& l,
                             float& r)
    {
        if (slot.drive > 0.0f)
        {
            const auto gain = dsp::driveGain (slot.drive);
            const auto makeup = dsp::driveCompensation (slot.drive);

            l = dsp::softClip (l * gain) * makeup;
            r = dsp::softClip (r * gain) * makeup;
        }

        l = leftFilter.processSample (l);
        r = rightFilter.processSample (r);
    };

    if (filters.routing == FilterRouting::series)
    {
        runSlot (filters.filter1, filter1Left, filter1Right, left, right);
        runSlot (filters.filter2, filter2Left, filter2Right, left, right);
        return;
    }

    // Parallel: both slots see the same input.
    auto leftA = left;
    auto rightA = right;
    runSlot (filters.filter1, filter1Left, filter1Right, leftA, rightA);

    auto leftB = left;
    auto rightB = right;
    runSlot (filters.filter2, filter2Left, filter2Right, leftB, rightB);

    // Halved, so two identical filters in parallel are as loud as one rather
    // than twice as loud — which would put the engine's headroom measurements
    // out by 6 dB the moment a user switched routing (ADR-0025).
    left = (leftA + leftB) * 0.5f;
    right = (rightA + rightB) * 0.5f;
}

void Voice::setSources (const VoiceSourceSettings& newSources) noexcept
{
    // The overwhelmingly common case: the parameters did not move this block.
    if (newSources == sources)
        return;

    // Re-deriving a frequency reselects a mip level for every unison voice, so
    // it is done only when something that changes pitch actually moved.
    const bool pitchChanged = newSources.osc1.tuneSemitones != sources.osc1.tuneSemitones
                           || newSources.osc2.tuneSemitones != sources.osc2.tuneSemitones
                           || newSources.subOctave != sources.subOctave;

    sources = newSources;

    oscillator1.setTable (sources.osc1.table);
    oscillator1.setLayout (sources.osc1.unison);
    oscillator1.setPosition (sources.osc1.position);

    oscillator2.setTable (sources.osc2.table);
    oscillator2.setLayout (sources.osc2.unison);
    oscillator2.setPosition (sources.osc2.position);

    subOscillator.setTable (sources.subTable);

    updateGainTargets();

    if (pitchChanged && stage != VoiceStage::idle)
        updatePhaseIncrement();
}

void Voice::updateGainTargets() noexcept
{
    gain1.setTargets (sources.osc1.level, sources.osc1.pan);
    gain2.setTargets (sources.osc2.level, sources.osc2.pan);

    // The sub is mono and centred, and the noise generator is already stereo by
    // construction, so neither takes a balance.
    subGain.setTargets (sources.subLevel, 0.0f);
    noiseGain.setTargets (sources.noiseLevel, 0.0f);
}

void Voice::snapGainsToTargets() noexcept
{
    gain1.snap();
    gain2.snap();
    subGain.snap();
    noiseGain.snap();
}

void Voice::reset() noexcept
{
    stage = VoiceStage::idle;

    oscillator1.resetPhase (startPhase);
    oscillator2.resetPhase (startPhase);
    subOscillator.resetPhase (startPhase);

    oscillator1.setFrequency (0.0);
    oscillator2.setFrequency (0.0);
    subOscillator.setFrequency (0.0);

    // Re-seeding here rather than only in prepare makes reset genuinely
    // restore the voice to a known state: a render after a reset produces the
    // same noise as a render after preparation, which is what lets noise be
    // asserted on rather than merely eyeballed.
    noise.setSeed (noiseSeed);

    // Gains are parameter state, not note state, so they are snapped to their
    // targets rather than zeroed — a silent voice must not have to ramp its
    // level up from zero when it is next allocated.
    snapGainsToTargets();

    amplitudeEnvelope.reset();
    stealGain = 1.0f;

    // Integrators cleared with the voice, so a reused voice cannot start with
    // the tail of the note before it still ringing in the filter.
    filter1Left.reset();
    filter1Right.reset();
    filter2Left.reset();
    filter2Right.reset();

    note = -1;
    noteVelocity = 0.0f;
    sustainHeld = false;
    pendingNote = -1;
    pendingVelocity = 0.0f;
    pendingStartOrder = 0;

    // Pitch bend is a channel-wide value, not a per-note one, so it is
    // deliberately NOT cleared here: a voice reused while the wheel is held
    // must adopt the current bend, not snap back to centre.
}

void Voice::beginNote (int midiNote, float velocity, std::uint64_t newStartOrder) noexcept
{
    note = midiNote;
    noteVelocity = velocity;
    startOrder = newStartOrder;
    sustainHeld = false;

    // Every note restarts from this voice own fixed offset. Reproducible,
    // because the offset is fixed per voice rather than random, but decorrelated
    // across voices so a chord attack does not sum coherently. Free-running and
    // user-controlled phase remain open oscillator design choices.
    //
    // The noise generator is deliberately not restarted: free-running noise has
    // no attack transient of its own, and re-seeding it per note would give
    // every note of a part the identical noise burst.
    oscillator1.resetPhase (startPhase);
    oscillator2.resetPhase (startPhase);
    subOscillator.resetPhase (startPhase);

    // A voice that has been idle may hold gains from before the last parameter
    // move. Snapping means a fresh note begins at the level the user has dialled
    // in rather than gliding up to it.
    snapGainsToTargets();

    amplitudeEnvelope.noteOn();
    stealGain = 1.0f;
    stage = VoiceStage::sounding;

    updatePhaseIncrement();
}

void Voice::startNote (int midiNote, float velocity, std::uint64_t newStartOrder) noexcept
{
    pendingNote = -1;
    beginNote (midiNote, velocity, newStartOrder);
}

void Voice::steal (int midiNote, float velocity, std::uint64_t newStartOrder) noexcept
{
    if (stage == VoiceStage::idle)
    {
        startNote (midiNote, velocity, newStartOrder);
        return;
    }

    // Hold the new note until the fade-out completes.
    pendingNote = midiNote;
    pendingVelocity = velocity;
    pendingStartOrder = newStartOrder;
    stage = VoiceStage::stealing;

    // The stolen note no longer belongs to any held key.
    sustainHeld = false;
}

void Voice::releaseNote() noexcept
{
    if (stage == VoiceStage::sounding)
        amplitudeEnvelope.noteOff();
}

void Voice::setPitchBendSemitones (float semitones) noexcept
{
    pitchBendSemitones = semitones;

    if (stage != VoiceStage::idle)
        updatePhaseIncrement();
}

void Voice::updatePhaseIncrement() noexcept
{
    const double bentNote = static_cast<double> (note) + static_cast<double> (pitchBendSemitones);

    oscillator1.setFrequency (midiNoteToFrequency (bentNote + static_cast<double> (sources.osc1.tuneSemitones)));
    oscillator2.setFrequency (midiNoteToFrequency (bentNote + static_cast<double> (sources.osc2.tuneSemitones)));

    // The sub tracks the played note transposed by whole octaves. Clamped
    // rather than trusted: the parameter offers -1 and -2, but a corrupt preset
    // must not be able to place the sub outside the audible range.
    const auto octave = sources.subOctave < -4 ? -4 : (sources.subOctave > 0 ? 0 : sources.subOctave);
    subOscillator.setFrequency (midiNoteToFrequency (bentNote + 12.0 * static_cast<double> (octave)));
}

float Voice::nextAmplitude() noexcept
{
    if (stage == VoiceStage::idle)
        return 0.0f;

    if (stage == VoiceStage::stealing)
    {
        stealGain -= stealDecrement;

        if (stealGain <= 0.0f)
        {
            stealGain = 0.0f;

            // Silence reached, so the waiting note can start without a step.
            if (pendingNote >= 0)
            {
                const int nextNote = pendingNote;
                const float nextVelocity = pendingVelocity;
                const std::uint64_t nextOrder = pendingStartOrder;

                pendingNote = -1;
                beginNote (nextNote, nextVelocity, nextOrder);
            }
            else
            {
                reset();
            }

            return 0.0f;
        }

        // The envelope is deliberately frozen for the duration of the steal.
        // Advancing it as well would let it reach idle part-way through the
        // fade and drop the level to zero in one sample — a click, from the
        // very mechanism that exists to prevent one.
        return amplitudeEnvelope.getCurrentValue() * stealGain;
    }

    const auto value = amplitudeEnvelope.getNextValue();

    // The release has run its course, so the voice is free.
    if (! amplitudeEnvelope.isActive())
    {
        reset();
        return 0.0f;
    }

    return value;
}

void Voice::renderAdding (float* const* output, int numChannels, int startSample, int numSamples) noexcept
{
    if (stage == VoiceStage::idle || output == nullptr || numChannels <= 0)
        return;

    // Decided once per call rather than per sample. A source that is silent and
    // not ramping contributes nothing, so skipping it is exact, not an
    // approximation — and it is what keeps the default patch, which uses one of
    // the four sources, costing what it did before the other three existed.
    const bool renderOsc1 = sources.osc1.table != nullptr && gain1.isAudible();
    const bool renderOsc2 = sources.osc2.table != nullptr && gain2.isAudible();
    const bool renderSub = sources.subTable != nullptr && subGain.isAudible();
    const bool renderNoise = noiseGain.isAudible();

    for (int i = 0; i < numSamples; ++i)
    {
        const auto envelope = nextAmplitude();

        // The envelope may have idled the voice part-way through the block; the
        // remaining samples are silence and there is nothing left to add.
        if (stage == VoiceStage::idle && envelope <= 0.0f)
            break;

        float left = 0.0f;
        float right = 0.0f;

        if (renderOsc1)
        {
            float stackLeft = 0.0f;
            float stackRight = 0.0f;
            oscillator1.addNextStereoSample (stackLeft, stackRight);

            left += stackLeft * gain1.left.getNextValue();
            right += stackRight * gain1.right.getNextValue();
        }

        if (renderOsc2)
        {
            float stackLeft = 0.0f;
            float stackRight = 0.0f;
            oscillator2.addNextStereoSample (stackLeft, stackRight);

            left += stackLeft * gain2.left.getNextValue();
            right += stackRight * gain2.right.getNextValue();
        }

        if (renderSub)
        {
            const auto value = subOscillator.getNextSample();

            left += value * subGain.left.getNextValue();
            right += value * subGain.right.getNextValue();
        }

        if (renderNoise)
            noise.addNextStereoSample (left, right,
                                       noiseGain.left.getNextValue(),
                                       noiseGain.right.getNextValue());

        // Filter before the amplifier, which is the classic subtractive order and
        // not an arbitrary one: filtering after the envelope would make a
        // resonant tail fade with the note rather than ring through it, and
        // would gate a self-oscillating filter with its own note.
        if (! filters.isBypassed())
            applyFilters (left, right);

        const auto amplitude = envelope * noteVelocity;

        left *= amplitude;
        right *= amplitude;

        const auto index = startSample + i;

        if (numChannels == 1)
        {
            output[0][index] += (left + right) * 0.5f;
            continue;
        }

        output[0][index] += left;
        output[1][index] += right;

        // Layouts wider than stereo are rejected by the processor, so this loop
        // never runs in practice. It exists so that a voice driven directly —
        // by a test, or by a future renderer — produces sound on every channel
        // it was given rather than silence on all but two.
        if (numChannels > 2)
        {
            const auto mono = (left + right) * 0.5f;

            for (int channel = 2; channel < numChannels; ++channel)
                output[channel][index] += mono;
        }
    }
}

} // namespace apollo::engine
