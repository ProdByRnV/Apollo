#include "DSP/Effects/EffectsRack.h"

namespace apollo::dsp
{

bool EffectsRack::isImplemented (EffectType type) noexcept
{
    switch (type)
    {
        case EffectType::distortion:
            return true;

        // Phases 8b to 8e. Selectable now because the parameter's range is
        // permanent; silent until the DSP behind them lands.
        case EffectType::delay:
        case EffectType::reverb:
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
        case EffectType::reverb:
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

    refreshReporting();
}

void EffectsRack::reset() noexcept
{
    distortionUnit.reset();
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

    refreshReporting();
}

void EffectsRack::refreshReporting() noexcept
{
    latencySamples = 0;
    tailSeconds = 0.0;

    for (const auto& slot : chain)
    {
        auto* effect = effectFor (slot.effect);

        if (effect == nullptr)
            continue;

        latencySamples += effect->getLatencySamples();

        // Tail, unlike latency, belongs to the active path: a bypassed effect
        // is not ringing out, so it has nothing for an offline render to wait
        // for.
        if (! slot.bypassed)
        {
            const auto tail = effect->getTailSeconds();
            tailSeconds = tail > tailSeconds ? tail : tailSeconds;
        }
    }
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
