#include "DSP/Effects/EffectsRack.h"

namespace apollo::dsp
{

bool EffectsRack::isImplemented (EffectType type) noexcept
{
    switch (type)
    {
        case EffectType::distortion:
        case EffectType::delay:
        case EffectType::reverb:
            return true;

        // Phases 8d and 8e. Selectable now because the parameter's range is
        // permanent; silent until the DSP behind them lands.
        case EffectType::gate:
        case EffectType::compressor:
        case EffectType::equaliser:
        case EffectType::none:
        default:
            return false;
    }
}

AudioEffect* EffectsRack::effectFor (EffectType type) noexcept
{
    switch (type)
    {
        case EffectType::distortion:
            return &distortionUnit;

        case EffectType::delay:
            return &delayUnit;

        case EffectType::reverb:
            return &reverbUnit;

        case EffectType::gate:
        case EffectType::compressor:
        case EffectType::equaliser:
        case EffectType::none:
        default:
            return nullptr;
    }
}

void EffectsRack::prepare (double sampleRate, int maxBlockSize)
{
    distortionUnit.prepare (sampleRate, maxBlockSize);
    delayUnit.prepare (sampleRate, maxBlockSize);
    reverbUnit.prepare (sampleRate, maxBlockSize);

    refreshLatency();
}

void EffectsRack::reset() noexcept
{
    distortionUnit.reset();
    delayUnit.reset();
    reverbUnit.reset();
}

void EffectsRack::setTempo (double bpm) noexcept
{
    // Handed to every effect that syncs rather than only to the ones currently
    // in the chain: an effect put into a slot mid-bar must already know what
    // tempo it is at, not find out on the next block.
    delayUnit.setTempo (bpm);
}

void EffectsRack::setChain (const Chain& newChain) noexcept
{
    Chain resolved {};

    for (std::size_t slot = 0; slot < resolved.size(); ++slot)
    {
        const auto requested = newChain[slot];

        if (! isImplemented (requested.effect))
            continue;

        // One of each. A second occurrence of an effect already placed earlier
        // in the chain leaves this slot empty rather than running the same
        // object twice, which would feed its own output back into its own
        // state.
        auto alreadyPlaced = false;

        for (std::size_t earlier = 0; earlier < slot; ++earlier)
            alreadyPlaced = alreadyPlaced || resolved[earlier].effect == requested.effect;

        if (alreadyPlaced)
            continue;

        resolved[slot] = requested;
    }

    if (resolved == chain)
        return;

    chain = resolved;

    refreshLatency();
}

const AudioEffect* EffectsRack::effectFor (EffectType type) const noexcept
{
    return const_cast<EffectsRack*> (this)->effectFor (type);
}

void EffectsRack::refreshLatency() noexcept
{
    latencySamples = 0;

    for (const auto& slot : chain)
    {
        const auto* effect = effectFor (slot.effect);

        if (effect != nullptr)
            latencySamples += effect->getLatencySamples();
    }
}

double EffectsRack::getTailSeconds() const noexcept
{
    auto longest = 0.0;

    for (const auto& slot : chain)
    {
        // Tail, unlike latency, belongs to the active path: a bypassed effect
        // is not ringing out, so it has nothing for an offline render to wait
        // for.
        if (slot.bypassed)
            continue;

        const auto* effect = effectFor (slot.effect);

        if (effect == nullptr)
            continue;

        const auto tail = effect->getTailSeconds();
        longest = tail > longest ? tail : longest;
    }

    return longest;
}

void EffectsRack::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    if (channels == nullptr || numChannels <= 0 || numSamples <= 0)
        return;

    for (const auto& slot : chain)
    {
        auto* effect = effectFor (slot.effect);

        if (effect == nullptr)
            continue;

        if (slot.bypassed)
            effect->processBypassed (channels, numChannels, numSamples);
        else
            effect->process (channels, numChannels, numSamples);
    }
}

} // namespace apollo::dsp
