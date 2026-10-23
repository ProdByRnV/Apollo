#include "Engine/Voice.h"

#include <algorithm>
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

/** One filter slot — drive, filter, mix — on one stereo sample, in place.

    A lambda rather than a member function, as it was when it lived inside
    `applyFilters`. As a member function the default patch measured 6 to 8 per
    cent slower, at 8 voices and at 32, and as a lambda it measures as it did
    before (ADR-0093) — consistent with the function no longer being inlined
    into the per-sample loop that calls it twice.
*/
constexpr auto runFilterSlot = [] (float drive,
                                   dsp::VoiceFilter& leftFilter,
                                   dsp::VoiceFilter& rightFilter,
                                   dsp::LinearSmoothedValue& mix,
                                   float& l,
                                   float& r) noexcept
{
    const auto dryLeft = l;
    const auto dryRight = r;

    if (drive > 0.0f)
    {
        const auto gain = dsp::driveGain (drive);
        const auto makeup = dsp::driveCompensation (drive);

        l = dsp::softClip (l * gain) * makeup;
        r = dsp::softClip (r * gain) * makeup;
    }

    l = leftFilter.processSample (l);
    r = rightFilter.processSample (r);

    // Fully wet and not moving is the slot as it was before mix existed,
    // and is left exactly so: `dry + 1 * (wet - dry)` is not always `wet`
    // in floating point, and the regression renders would notice.
    if (mix.isSmoothing() || mix.getTargetValue() < 1.0f)
    {
        const auto wet = mix.getNextValue();

        l = dryLeft + wet * (l - dryLeft);
        r = dryRight + wet * (r - dryRight);
    }
};

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
    noise.prepare (sampleRate);

    for (auto& envelope : envelopes)
        envelope.prepare (sampleRate);

    for (auto& lfo : lfos)
        lfo.prepare (sampleRate);

    stealDecrement = static_cast<float> (rampIncrement (stealSeconds, sampleRate));

    filter1Mix.reset (sampleRate, gainRampSeconds);
    filter2Mix.reset (sampleRate, gainRampSeconds);

    filter1Left.prepare (sampleRate);
    filter1Right.prepare (sampleRate);
    filter2Left.prepare (sampleRate);
    filter2Right.prepare (sampleRate);

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

    filter1Left.setType (filters.filter1.mode);
    filter1Right.setType (filters.filter1.mode);
    filter2Left.setType (filters.filter2.mode);
    filter2Right.setType (filters.filter2.mode);

    updateFilterControls();

    // Through the voice's own resolution rather than straight to the shared
    // set, so a voice being modulated or key-tracked keeps its offset across a
    // parameter move instead of spending a control block without it.
    resolveFilterCoefficients();
}

void Voice::applyFilters (float& left, float& right) noexcept
{
    if (filters.routing == FilterRouting::series)
    {
        runFilterSlot (filter1Drive, filter1Left, filter1Right, filter1Mix, left, right);
        runFilterSlot (filter2Drive, filter2Left, filter2Right, filter2Mix, left, right);
        return;
    }

    // Parallel: both slots see the same input.
    auto leftA = left;
    auto rightA = right;
    runFilterSlot (filter1Drive, filter1Left, filter1Right, filter1Mix, leftA, rightA);

    auto leftB = left;
    auto rightB = right;
    runFilterSlot (filter2Drive, filter2Left, filter2Right, filter2Mix, leftB, rightB);

    // Halved, so two identical filters in parallel are as loud as one rather
    // than twice as loud — which would put the engine's headroom measurements
    // out by 6 dB the moment a user switched routing (ADR-0025).
    left = (leftA + leftB) * 0.5f;
    right = (rightA + rightB) * 0.5f;
}

void Voice::applySplitFilters (float& left, float& right,
                               float secondLeft, float secondRight,
                               float directLeft, float directRight) noexcept
{
    // Both slots run every sample whether or not anything feeds them, so a
    // filter a source has just left rings out instead of stopping dead.
    runFilterSlot (filter1Drive, filter1Left, filter1Right, filter1Mix, left, right);
    runFilterSlot (filter2Drive, filter2Left, filter2Right, filter2Mix, secondLeft, secondRight);

    // Summed, not halved: each source took one path, so this is the sources'
    // own sum when both filters are transparent.
    left = left + secondLeft + directLeft;
    right = right + secondRight + directRight;
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

        // A source that only scales another routing still has to run.
        const auto via = static_cast<std::size_t> (slot.via);

        if (slot.via != dsp::ModSource::none && via < sourceUsed.size())
            sourceUsed[via] = true;
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

float Voice::getOscillatorPosition (int index) const noexcept
{
    using Destination = dsp::ModDestination;

    if (index == 2)
        return clampFinite (sources.osc2.position + destinationOffset (Destination::osc2Position),
                            0.0f, 1.0f);

    return clampFinite (sources.osc1.position + destinationOffset (Destination::osc1Position),
                        0.0f, 1.0f);
}

float Voice::getOscillatorWarpAmount (int index) const noexcept
{
    using Destination = dsp::ModDestination;

    if (index == 2)
        return clampFinite (sources.osc2.warpAmount + destinationOffset (Destination::osc2Warp),
                            0.0f, 1.0f);

    return clampFinite (sources.osc1.warpAmount + destinationOffset (Destination::osc1Warp),
                        0.0f, 1.0f);
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

    // The wheel position itself, not the semitones it produced. The bend range
    // is a control now, and a source derived by dividing by it would change
    // meaning the moment someone widened the range.
    store (Source::pitchBend, pitchBendNormalised);

    // Per-note pressure, which for a plain controller is just the channel
    // pressure every sounding voice was handed.
    store (Source::aftertouch, pressure);

    // Timbre is a position with a centre, so the source is bipolar around it: an
    // untouched controller reads zero rather than -1.
    store (Source::timbre, timbre * 2.0f - 1.0f);

    store (Source::random, perNoteRandom);

    for (int i = 0; i < dsp::numMacros; ++i)
        store (static_cast<Source> (static_cast<int> (Source::macro1) + i), macroValues[static_cast<std::size_t> (i)]);

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

    // The warp amount, offset by its routings, every modulation block like the
    // position. setWarp returns at once when nothing moved, which with no
    // routing on warp is every block.
    oscillator1.setWarp (sources.osc1.warpMode,
                         clampFinite (sources.osc1.warpAmount + destinationOffset (Destination::osc1Warp),
                                      0.0f, 1.0f));
    oscillator2.setWarp (sources.osc2.warpMode,
                         clampFinite (sources.osc2.warpAmount + destinationOffset (Destination::osc2Warp),
                                      0.0f, 1.0f));

    // Levels and balances go to the smoothers rather than straight to a gain, so
    // a modulation arriving every sixteen samples still interpolates per sample.
    gain1.setTargets (sources.osc1.level + destinationOffset (Destination::osc1Level),
                      sources.osc1.pan + destinationOffset (Destination::osc1Pan));

    gain2.setTargets (sources.osc2.level + destinationOffset (Destination::osc2Level),
                      sources.osc2.pan + destinationOffset (Destination::osc2Pan));

    subGain.setTargets (sources.subLevel + destinationOffset (Destination::subLevel), 0.0f);
    noiseGain.setTargets (sources.noiseLevel + destinationOffset (Destination::noiseLevel), 0.0f);

    updateFilterControls();
    resolveFilterCoefficients();
}

void Voice::updateFilterControls() noexcept
{
    using Destination = dsp::ModDestination;

    // Each slot's drive and mix, offset by the matrix (Phase 13e-4). With no
    // routing the offsets are zero and these are the settings exactly. The mix
    // goes to its smoother, so a routing arriving every sixteen samples still
    // glides; the drive is read per sample and steps with the control block,
    // as the cutoff does.
    filter1Drive = clampFinite (filters.filter1.drive + destinationOffset (Destination::filter1Drive), 0.0f, 1.0f);
    filter2Drive = clampFinite (filters.filter2.drive + destinationOffset (Destination::filter2Drive), 0.0f, 1.0f);

    filter1Mix.setTargetValue (clampFinite (filters.filter1.mix + destinationOffset (Destination::filter1Mix), 0.0f, 1.0f));
    filter2Mix.setTargetValue (clampFinite (filters.filter2.mix + destinationOffset (Destination::filter2Mix), 0.0f, 1.0f));
}

void Voice::resolveFilterCoefficients() noexcept
{
    using Destination = dsp::ModDestination;

    // Key tracking follows what was played, so a comb tuned to the note stays
    // on it through a bend; a vibrato is a modulation of the pitch, not a
    // different note. An idle voice has no note, and keeps the shared set.
    const auto playedOctaves = this->playedOctaves();

    // Filters re-resolve their own coefficients only when something actually
    // reaches them — the matrix or key tracking. Otherwise the engine's shared
    // set is used unchanged, and the unmodulated path never computes a tangent.
    const auto resolveFilter = [this, playedOctaves] (const FilterSlotSettings& slot,
                                                      Destination cutoffTarget,
                                                      Destination resonanceTarget,
                                                      Destination keyTrackTarget,
                                                      dsp::VoiceFilter& left,
                                                      dsp::VoiceFilter& right)
    {
        const auto cutoffOffset = destinationOffset (cutoffTarget);
        const auto resonanceOffset = destinationOffset (resonanceTarget);
        const auto keyTrack = clampFinite (slot.keyTrack + destinationOffset (keyTrackTarget), -1.0f, 1.0f);
        const auto trackedOctaves = keyTrack * playedOctaves;

        if (cutoffOffset == 0.0f && resonanceOffset == 0.0f && trackedOctaves == 0.0f)
        {
            left.setCoefficients (slot.coefficients);
            right.setCoefficients (slot.coefficients);
            return;
        }

        // Cutoff moves in octaves, from the matrix and from the keyboard alike.
        // A fixed number of hertz would be a huge interval at the bottom of the
        // range and inaudible at the top, so the same depth would mean something
        // different at every cutoff setting.
        const auto cutoff = slot.cutoffHz * std::exp2 (cutoffOffset + trackedOctaves);
        const auto q = slot.q + resonanceOffset;

        dsp::VoiceFilterCoefficients coefficients;
        coefficients.set (slot.mode, cutoff, q, sampleRate);

        left.setCoefficients (coefficients);
        right.setCoefficients (coefficients);
    };

    resolveFilter (filters.filter1, Destination::filter1Cutoff, Destination::filter1Resonance,
                   Destination::filter1KeyTrack, filter1Left, filter1Right);

    resolveFilter (filters.filter2, Destination::filter2Cutoff, Destination::filter2Resonance,
                   Destination::filter2KeyTrack, filter2Left, filter2Right);
}

float Voice::playedOctaves() const noexcept
{
    if (stage == VoiceStage::idle)
        return 0.0f;

    return (static_cast<float> (note - 60) + pitchBendSemitones + noteBendSemitones) / 12.0f;
}

void Voice::updateNoiseRate() noexcept
{
    // Zero tracking is the rate the noise always had, set without a power of
    // two, so an untracked note never touches the interpolating path.
    if (sources.noiseKeyTrack == 0.0f)
    {
        noise.setRate (1.0f);
        return;
    }

    noise.setRate (std::exp2 (sources.noiseKeyTrack * playedOctaves()));
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
                           || newSources.subOctave != sources.subOctave
                           || newSources.subTracksOsc1 != sources.subTracksOsc1;

    sources = newSources;

    oscillator1.setTable (sources.osc1.table);
    oscillator1.setLayout (sources.osc1.unison);
    oscillator1.setPosition (sources.osc1.position);
    oscillator1.setWarp (sources.osc1.warpMode, sources.osc1.warpAmount);

    oscillator2.setTable (sources.osc2.table);
    oscillator2.setLayout (sources.osc2.unison);
    oscillator2.setPosition (sources.osc2.position);
    oscillator2.setWarp (sources.osc2.warpMode, sources.osc2.warpAmount);

    subOscillator.setTable (sources.subTable);

    noise.setType (sources.noiseType, sources.noiseTransient);
    noise.setLoop (sources.noiseLoop);
    updateNoiseRate();

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

    filter1Mix.settle();
    filter2Mix.settle();
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
    channel = 0;
    sustainHeld = false;
    pendingNote = -1;
    pendingVelocity = 0.0f;
    pendingChannel = 0;
    pendingStartOrder = 0;

    // Per-note expression belongs to the note, so it goes with it. Timbre
    // returns to its centre rather than to zero: it is a position, and zero is
    // one end of its travel rather than its rest.
    pressure = 0.0f;
    timbre = 0.5f;
    noteBendSemitones = 0.0f;

    // Pitch bend is a channel-wide value, not a per-note one, so it is
    // deliberately NOT cleared here: a voice reused while the wheel is held
    // must adopt the current bend, not snap back to centre.
}

void Voice::beginNote (int midiNote, float velocity, std::uint64_t newStartOrder,
                       int midiChannel) noexcept
{
    note = midiNote;
    noteVelocity = velocity;
    startOrder = newStartOrder;
    channel = midiChannel;
    sustainHeld = false;

    // Pressure starts at zero whatever the previous note on this voice left
    // behind: it is a *force*, nobody is applying one at the instant a note
    // begins, and inheriting a held pressure from a note that has already ended
    // would be loudly wrong in a way nothing else here is (ADR-0044).
    //
    // Timbre and the per-note bend are deliberately not touched. They are
    // positions rather than forces, and the caller sets them for this note —
    // which is what lets an MPE controller place a note's pitch before sending
    // the note-on, as every one of them does.
    pressure = 0.0f;

    // Every note restarts from this voice's own fixed offset. Reproducible,
    // because the offset is fixed per voice rather than random, but decorrelated
    // across voices so a chord attack does not sum coherently (ADR-0017).
    //
    // Phase 13a adds the user's start phase for each source on top, and for
    // the primary oscillators a random scatter of their unison voices. Both
    // default to zero: the phase added is exactly 0.0 and the scatter is skipped
    // rather than multiplied by nothing, so a note starts exactly where it
    // always did. The scatter is a hash of this voice's random state, which
    // advances once per note, so it differs from note to note and still
    // renders identically every time the same notes are played.
    //
    // The noise generator is deliberately not restarted: free-running noise has
    // no attack transient of its own, and re-seeding it per note would give
    // every note of a part the identical noise burst.
    oscillator1.resetPhase (startPhase + static_cast<double> (sources.osc1.startPhase),
                            sources.osc1.phaseRandom, randomState);
    oscillator2.resetPhase (startPhase + static_cast<double> (sources.osc2.startPhase),
                            sources.osc2.phaseRandom, randomState ^ 0x5BD1E995u);
    subOscillator.resetPhase (startPhase + static_cast<double> (sources.subPhase));

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

    // After the stage, which is what gives the voice a played pitch to follow.
    updateNoiseRate();

    // A transient starts with the note, at the rate just set. A stream runs on.
    noise.trigger();

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

        // A key-tracked filter needs this note's pitch before the first sample;
        // with a routing, applyModulation above has already done it.
        if (isKeyTracking())
            resolveFilterCoefficients();
    }
}

void Voice::startNote (int midiNote, float velocity, std::uint64_t newStartOrder,
                       int midiChannel) noexcept
{
    pendingNote = -1;
    beginNote (midiNote, velocity, newStartOrder, midiChannel);
}

void Voice::steal (int midiNote, float velocity, std::uint64_t newStartOrder,
                   int midiChannel) noexcept
{
    if (stage == VoiceStage::idle)
    {
        startNote (midiNote, velocity, newStartOrder, midiChannel);
        return;
    }

    // Hold the new note until the fade-out completes.
    pendingNote = midiNote;
    pendingVelocity = velocity;
    pendingChannel = midiChannel;
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
    {
        updatePhaseIncrement();
        updateNoiseRate();

        if (isKeyTracking())
            resolveFilterCoefficients();
    }
}

void Voice::setNoteBendSemitones (float semitones) noexcept
{
    noteBendSemitones = semitones;

    if (stage != VoiceStage::idle)
    {
        updatePhaseIncrement();
        updateNoiseRate();

        if (isKeyTracking())
            resolveFilterCoefficients();
    }
}

void Voice::updatePhaseIncrement() noexcept
{
    using Destination = dsp::ModDestination;

    // `allPitch` reaches every source, which is what a vibrato wants; the
    // per-oscillator destinations then detune one against the other on top of it.
    const auto commonPitch = static_cast<double> (destinationOffset (Destination::allPitch));

    // The channel-wide bend and this note's own bend add. Under MPE both are in
    // play at once — a wheel on the manager channel moves the whole zone through
    // its own range while a finger slides one note through a much wider one — and
    // outside MPE the per-note term is simply zero.
    const double bentNote = static_cast<double> (note)
                          + static_cast<double> (pitchBendSemitones)
                          + static_cast<double> (noteBendSemitones)
                          + commonPitch;

    const double osc1Note = bentNote + static_cast<double> (sources.osc1.tuneSemitones)
                          + static_cast<double> (destinationOffset (Destination::osc1Pitch));

    oscillator1.setFrequency (midiNoteToFrequency (osc1Note));

    oscillator2.setFrequency (midiNoteToFrequency (
        bentNote + static_cast<double> (sources.osc2.tuneSemitones)
                 + static_cast<double> (destinationOffset (Destination::osc2Pitch))));

    // The sub tracks the played note — or, with its tracking set to oscillator
    // 1, that oscillator's own pitch, tuning and pitch modulation included —
    // transposed by whole octaves. Clamped rather than trusted: the parameter
    // offers -1 and -2, but a corrupt preset must not be able to place the sub
    // outside the audible range.
    const auto octave = sources.subOctave < -4 ? -4 : (sources.subOctave > 0 ? 0 : sources.subOctave);
    const double subBase = sources.subTracksOsc1 ? osc1Note : bentNote;

    subOscillator.setFrequency (midiNoteToFrequency (subBase + 12.0 * static_cast<double> (octave)));
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
                const int nextChannel = pendingChannel;
                const std::uint64_t nextOrder = pendingStartOrder;

                pendingNote = -1;
                beginNote (nextNote, nextVelocity, nextOrder, nextChannel);

                // An amplitude envelope that continues picks up at the level the
                // last note left, and the voice is silent here, so going straight
                // there would be a step. The same ramp that took it down brings
                // it back up instead. One that restarts begins at silence anyway.
                if (envelopes[0].getSettings().continueOnRetrigger)
                    stealGain = 0.0f;
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

    // Coming back up after a steal into a continuing envelope (above). Below
    // one only then, so every other note takes the line after unchanged.
    if (stealGain < 1.0f)
    {
        stealGain = std::min (1.0f, stealGain + stealDecrement);
        return value * stealGain;
    }

    return value;
}

void Voice::renderAdding (float* const* output, int numChannels, int startSample, int numSamples,
                          const VoiceTaps& taps) noexcept
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
    const bool renderNoise = noiseGain.isAudible() && noise.isSounding();

    // Which bus each source feeds, decided once per call. Anything that is not
    // one of the three inputs lands on filter 1, as the processor's clamp
    // would have put it, rather than wherever the last branch below sends it.
    const bool split = filters.routing == FilterRouting::split;

    const auto busOf = [split] (FilterInput input) noexcept
    {
        const auto index = static_cast<int> (input);
        return split && index > 0 && index < numFilterInputs ? index : 0;
    };

    const auto osc1Bus = busOf (filters.inputs.osc1);
    const auto osc2Bus = busOf (filters.inputs.osc2);
    const auto subBus = busOf (filters.inputs.sub);
    const auto noiseBus = busOf (filters.inputs.noise);

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

        // A bus per filter input: filter 1's, filter 2's, and past both.
        // Outside split routing every source lands on the first, in the order
        // it always did, so the sum is the one it was. Scalars and a branch on
        // a value fixed for the whole call, rather than an array indexed by
        // it, so the sums can stay in registers.
        float left = 0.0f;
        float right = 0.0f;
        float secondLeft = 0.0f;
        float secondRight = 0.0f;
        float directLeft = 0.0f;
        float directRight = 0.0f;

        const auto send = [&] (int bus, float l, float r) noexcept
        {
            if (bus == 0)
            {
                left += l;
                right += r;
            }
            else if (bus == 1)
            {
                secondLeft += l;
                secondRight += r;
            }
            else
            {
                directLeft += l;
                directRight += r;
            }
        };

        if (renderOsc1)
        {
            float stackLeft = 0.0f;
            float stackRight = 0.0f;
            oscillator1.addNextStereoSample (stackLeft, stackRight);

            // Scaled in place rather than on the way into the mix, so the same
            // two multiplies produce both the contribution and the tap. The
            // arithmetic is identical to what this line did before taps
            // existed; only the order of the statements has moved.
            stackLeft *= gain1.left.getNextValue();
            stackRight *= gain1.right.getNextValue();

            send (osc1Bus, stackLeft, stackRight);

            if (taps.oscillator1 != nullptr)
                taps.oscillator1[i] += (stackLeft + stackRight) * 0.5f;
        }

        if (renderOsc2)
        {
            float stackLeft = 0.0f;
            float stackRight = 0.0f;
            oscillator2.addNextStereoSample (stackLeft, stackRight);

            stackLeft *= gain2.left.getNextValue();
            stackRight *= gain2.right.getNextValue();

            send (osc2Bus, stackLeft, stackRight);

            if (taps.oscillator2 != nullptr)
                taps.oscillator2[i] += (stackLeft + stackRight) * 0.5f;
        }

        if (renderSub)
        {
            const auto value = subOscillator.getNextSample();

            const auto subLeft = value * subGain.left.getNextValue();
            const auto subRight = value * subGain.right.getNextValue();

            send (subBus, subLeft, subRight);

            if (taps.sub != nullptr)
                taps.sub[i] += (subLeft + subRight) * 0.5f;
        }

        if (renderNoise)
        {
            // The generator adds into its destination, so the obvious thing is
            // to have it add straight into the mix when nothing is watching and
            // through a local when something is. That is two code paths, and on
            // a target whose compiler contracts a multiply and an add into a
            // single FMA they do not round the same way: adding into the mix
            // rounds once, going through a local rounds twice. The difference is
            // about 1e-8 — far below anything audible, and *exactly* the wrong
            // thing to accept, because "watching a source does not change it" is
            // worth having as an equality rather than an approximation. It is
            // checked as one (Tests/Telemetry/ScopeTests.cpp), and the check is
            // only meaningful if there is one path to check.
            //
            // So the local is unconditional and the branch buys only the store.
            const auto noiseLeftGain = noiseGain.left.getNextValue();
            const auto noiseRightGain = noiseGain.right.getNextValue();

            float noiseLeft = 0.0f;
            float noiseRight = 0.0f;
            noise.addNextStereoSample (noiseLeft, noiseRight, noiseLeftGain, noiseRightGain);

            send (noiseBus, noiseLeft, noiseRight);

            if (taps.noise != nullptr)
                taps.noise[i] += (noiseLeft + noiseRight) * 0.5f;
        }

        // Filter before the amplifier, which is the classic subtractive order and
        // not an arbitrary one: filtering after the envelope would make a
        // resonant tail fade with the note rather than ring through it, and
        // would gate a self-oscillating filter with its own note.
        if (split)
        {
            if (filters.isBypassed())
            {
                left = left + secondLeft + directLeft;
                right = right + secondRight + directRight;
            }
            else
            {
                applySplitFilters (left, right, secondLeft, secondRight, directLeft, directRight);
            }
        }
        else if (! filters.isBypassed())
        {
            applyFilters (left, right);
        }

        const auto amplitude = envelope * noteVelocity * amplitudeScale;

        left *= amplitude;
        right *= amplitude;

        // Taken here rather than immediately after the filter, because this is
        // the point at which the voice is finished: everything it does to the
        // signal has been done, and the only thing left is addition. Today that
        // makes this scope the output scope without the master gain; once the
        // effects rack exists it becomes the dry instrument against the
        // processed one, which is the comparison it is really for.
        if (taps.postFilter != nullptr)
            taps.postFilter[i] += (left + right) * 0.5f;

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

            // Named for what it is rather than "channel": a Voice now also has
            // a MIDI channel, and two different things called the same thing a
            // few lines apart is how a bug gets written.
            for (int outputChannel = 2; outputChannel < numChannels; ++outputChannel)
                output[outputChannel][index] += mono;
        }
    }
}

} // namespace apollo::engine
