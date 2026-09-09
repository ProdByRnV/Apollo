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

    for (auto& envelope : envelopes)
        envelope.prepare (sampleRate);

    for (auto& lfo : lfos)
        lfo.prepare (sampleRate);

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
    envelopes[0].setSettings (newSettings);
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

void Voice::setEnvelopeSettings (int index, const dsp::EnvelopeSettings& newSettings) noexcept
{
    if (index < 0 || index >= numEnvelopes)
        return;

    envelopes[static_cast<std::size_t> (index)].setSettings (newSettings);
}

void Voice::setLfoSettings (int index, const dsp::LfoSettings& newSettings) noexcept
{
    if (index < 0 || index >= numLfos)
        return;

    lfos[static_cast<std::size_t> (index)].setSettings (newSettings);
}

void Voice::setModulationRouting (const dsp::ModulationRouting& newRouting) noexcept
{
    if (newRouting == routing)
        return;

    routing = newRouting;

    hasActiveRouting = false;
    sourceUsed.fill (false);

    for (const auto& slot : routing.slots)
    {
        if (! slot.isActive())
            continue;

        hasActiveRouting = true;

        const auto index = static_cast<std::size_t> (slot.source);

        if (index < sourceUsed.size())
            sourceUsed[index] = true;
    }

    // A routing that has just been switched off must not leave its last offsets
    // applied for ever.
    if (! hasActiveRouting)
    {
        destinationValues.fill (0.0f);
        applyModulation();
    }

    modulationCountdown = 0;
}

float Voice::getSourceValue (dsp::ModSource source) const noexcept
{
    const auto index = static_cast<std::size_t> (source);

    return index < sourceValues.size() ? sourceValues[index] : 0.0f;
}

void Voice::setModulationSeed (std::uint32_t seed) noexcept
{
    // Zero would lock a xorshift at zero for ever, so it is mapped away rather
    // than trusted.
    randomState = (seed * 2654435761u) | 1u;

    for (std::size_t i = 0; i < lfos.size(); ++i)
        lfos[i].setSeed (seed + static_cast<std::uint32_t> (i) * 7919u);
}

void Voice::advanceModulationSources() noexcept
{
    using Source = dsp::ModSource;

    // Envelope 0 is advanced by the amplitude path, which runs whether or not
    // anything routes it, so advancing it here as well would run it twice as
    // fast. The other three advance only if a slot reads them.
    for (int i = 1; i < numEnvelopes; ++i)
    {
        const auto source = static_cast<Source> (static_cast<int> (Source::envelope1) + i);

        if (sourceUsed[static_cast<std::size_t> (source)])
            static_cast<void> (envelopes[static_cast<std::size_t> (i)].getNextValue());
    }

    for (int i = 0; i < numLfos; ++i)
    {
        const auto source = static_cast<Source> (static_cast<int> (Source::lfo1) + i);

        if (sourceUsed[static_cast<std::size_t> (source)])
            static_cast<void> (lfos[static_cast<std::size_t> (i)].getNextValue());
    }
}

void Voice::updateModulation() noexcept
{
    using Source = dsp::ModSource;

    const auto store = [this] (Source source, float value)
    {
        sourceValues[static_cast<std::size_t> (source)] = value;
    };

    // Envelope 0 is advanced by the amplitude path, so it is read rather than
    // stepped here; the others advance only if something routes them.
    store (Source::envelope1, envelopes[0].getCurrentValue());
    store (Source::envelope2, envelopes[1].getCurrentValue());
    store (Source::envelope3, envelopes[2].getCurrentValue());
    store (Source::envelope4, envelopes[3].getCurrentValue());

    store (Source::lfo1, lfos[0].getCurrentValue());
    store (Source::lfo2, lfos[1].getCurrentValue());
    store (Source::lfo3, lfos[2].getCurrentValue());
    store (Source::lfo4, lfos[3].getCurrentValue());

    store (Source::velocity, noteVelocity);

    // Key tracking, centred on middle C and scaled so the playable range is
    // roughly -1 to +1. Bipolar, so tracking does the opposite thing below the
    // centre from what it does above it.
    store (Source::keyTrack, static_cast<float> (note - 60) / 48.0f);

    store (Source::modWheel, modWheel);
    store (Source::pitchBend, pitchBendSemitones / 2.0f);
    store (Source::aftertouch, aftertouch);
    store (Source::random, perNoteRandom);

    dsp::evaluateModulation (routing, sourceValues, destinationValues);
}

void Voice::applyModulation() noexcept
{
    using Destination = dsp::ModDestination;

    // Nothing here writes to `sources` or to `filters`. Those hold what the user
    // set, and modulation is an offset applied on the way to the thing that
    // consumes them — which is what makes the base values survive a modulated
    // note and makes turning modulation off restore exactly what was there.

    updatePhaseIncrement();

    // Amplitude modulation attenuates and never boosts.
    //
    // Clamped to unity at the top rather than allowed to reach two: the engine's
    // headroom is measured for a voice at full level (ADR-0025), and letting a
    // routing multiply that would put every gain-staging measurement out by up
    // to 6 dB the moment someone drew a tremolo. A patch that wants more level
    // raises the level control and modulates downward from it, which is what a
    // tremolo is anyway.
    amplitudeScale = clampFinite (1.0f + destinationOffset (Destination::amplitude), 0.0f, 1.0f);

    const auto position1 = clampFinite (sources.osc1.position + destinationOffset (Destination::osc1Position), 0.0f, 1.0f);
    const auto position2 = clampFinite (sources.osc2.position + destinationOffset (Destination::osc2Position), 0.0f, 1.0f);

    oscillator1.setPosition (position1);
    oscillator2.setPosition (position2);

    // Levels and balances go to the smoothers rather than straight to a gain, so
    // a modulation arriving every sixteen samples still interpolates per sample.
    gain1.setTargets (sources.osc1.level + destinationOffset (Destination::osc1Level),
                      sources.osc1.pan + destinationOffset (Destination::osc1Pan));

    gain2.setTargets (sources.osc2.level + destinationOffset (Destination::osc2Level),
                      sources.osc2.pan + destinationOffset (Destination::osc2Pan));

    subGain.setTargets (sources.subLevel + destinationOffset (Destination::subLevel), 0.0f);
    noiseGain.setTargets (sources.noiseLevel + destinationOffset (Destination::noiseLevel), 0.0f);

    // Filters re-resolve their own coefficients only when something actually
    // reaches them. Otherwise the engine's shared set is used unchanged, and the
    // unmodulated path never computes a tangent.
    const auto resolveFilter = [this] (const FilterSlotSettings& slot,
                                       Destination cutoffTarget,
                                       Destination resonanceTarget,
                                       dsp::StateVariableFilter& left,
                                       dsp::StateVariableFilter& right)
    {
        const auto cutoffOffset = destinationOffset (cutoffTarget);
        const auto resonanceOffset = destinationOffset (resonanceTarget);

        if (cutoffOffset == 0.0f && resonanceOffset == 0.0f)
        {
            left.setCoefficients (slot.coefficients);
            right.setCoefficients (slot.coefficients);
            return;
        }

        // Cutoff moves in octaves. A fixed number of hertz would be a huge
        // interval at the bottom of the range and inaudible at the top, so the
        // same depth would mean something different at every cutoff setting.
        const auto cutoff = slot.cutoffHz * std::exp2 (cutoffOffset);
        const auto q = slot.q + resonanceOffset;

        dsp::SvfCoefficients coefficients;
        coefficients.set (cutoff, q, sampleRate);

        left.setCoefficients (coefficients);
        right.setCoefficients (coefficients);
    };

    resolveFilter (filters.filter1, Destination::filter1Cutoff, Destination::filter1Resonance,
                   filter1Left, filter1Right);

    resolveFilter (filters.filter2, Destination::filter2Cutoff, Destination::filter2Resonance,
                   filter2Left, filter2Right);
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

    for (auto& envelope : envelopes)
        envelope.reset();

    for (auto& lfo : lfos)
        lfo.reset();

    // Offsets cleared with the voice. A reused voice must not begin with the
    // previous note's modulation still applied to its pitch or its cutoff.
    destinationValues.fill (0.0f);
    sourceValues.fill (0.0f);
    modulationCountdown = 0;
    perNoteRandom = 0.0f;
    amplitudeScale = 1.0f;

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

    for (auto& envelope : envelopes)
        envelope.noteOn();

    // Free-running LFOs adopt the engine's shared phase so that voices started
    // at different times move together; retriggering ones ignore it and restart.
    for (std::size_t i = 0; i < lfos.size(); ++i)
        lfos[i].noteOn (freeRunningLfoPhase[i]);

    // One random value, chosen now and held for the life of the note. This is
    // the source that makes repeated notes differ from one another, which is a
    // different job from the noise generator and from a sample-and-hold LFO.
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    perNoteRandom = static_cast<float> (static_cast<std::int32_t> (randomState)) / 2147483648.0f;

    stealGain = 1.0f;
    stage = VoiceStage::sounding;

    // Forces a modulation update on the first sample, so a note starts already
    // modulated rather than spending up to a control block at its unmodulated
    // value — which on a filter sweep is an audible blip at every note.
    modulationCountdown = 0;

    if (hasActiveRouting)
    {
        updateModulation();
        applyModulation();
    }
    else
    {
        updatePhaseIncrement();
    }
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
        for (auto& envelope : envelopes)
        envelope.noteOff();
}

void Voice::setPitchBendSemitones (float semitones) noexcept
{
    pitchBendSemitones = semitones;

    if (stage != VoiceStage::idle)
        updatePhaseIncrement();
}

void Voice::updatePhaseIncrement() noexcept
{
    using Destination = dsp::ModDestination;

    // `allPitch` reaches every source, which is what a vibrato wants; the
    // per-oscillator destinations then detune one against the other on top of it.
    const auto commonPitch = static_cast<double> (destinationOffset (Destination::allPitch));

    const double bentNote = static_cast<double> (note)
                          + static_cast<double> (pitchBendSemitones)
                          + commonPitch;

    oscillator1.setFrequency (midiNoteToFrequency (
        bentNote + static_cast<double> (sources.osc1.tuneSemitones)
                 + static_cast<double> (destinationOffset (Destination::osc1Pitch))));

    oscillator2.setFrequency (midiNoteToFrequency (
        bentNote + static_cast<double> (sources.osc2.tuneSemitones)
                 + static_cast<double> (destinationOffset (Destination::osc2Pitch))));

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
        return envelopes[0].getCurrentValue() * stealGain;
    }

    const auto value = envelopes[0].getNextValue();

    // The release has run its course, so the voice is free.
    if (! envelopes[0].isActive())
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
        if (hasActiveRouting)
        {
            // Sources advance every sample so their timing is exact — an
            // envelope stepped once per control block would run sixteen times
            // slow — but only the ones something actually routes. A patch using
            // one LFO does not pay for four.
            advanceModulationSources();

            if (--modulationCountdown <= 0)
            {
                modulationCountdown = modulationBlockSamples;

                updateModulation();
                applyModulation();
            }
        }

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

        const auto amplitude = envelope * noteVelocity * amplitudeScale;

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
