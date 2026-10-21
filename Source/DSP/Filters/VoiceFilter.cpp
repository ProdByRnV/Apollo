#include "DSP/Filters/VoiceFilter.h"

namespace apollo::dsp
{

namespace
{

[[nodiscard]] bool isStateVariable (FilterType type) noexcept
{
    return type == FilterType::lowpass || type == FilterType::highpass
        || type == FilterType::bandpass || type == FilterType::notch;
}

} // namespace

void VoiceFilterCoefficients::set (FilterType type, float cutoffHz, float q, double sampleRate) noexcept
{
    switch (type)
    {
        case FilterType::lowpass:
        case FilterType::highpass:
        case FilterType::bandpass:
        case FilterType::notch:
            svf.set (cutoffHz, q, sampleRate);
            break;

        case FilterType::comb:
            comb.set (cutoffHz, q, sampleRate);
            break;

        case FilterType::formant:
            formant.set (cutoffHz, q, sampleRate);
            break;

        case FilterType::off:
        default:
            break;
    }
}

void VoiceFilter::prepare (double sampleRate)
{
    comb.prepare (sampleRate);
    reset();
}

void VoiceFilter::reset() noexcept
{
    svf.reset();
    comb.reset();
    formant.reset();
}

void VoiceFilter::setType (FilterType newType) noexcept
{
    if (newType == type)
        return;

    // Between the state-variable types, and through off, the integrators carry
    // over as they did before 13c. Coming back from the comb or the formant
    // filter, they hold a note from before the switch, and start from silence.
    if (newType == FilterType::comb)
        comb.reset();
    else if (newType == FilterType::formant)
        formant.reset();
    else if (type == FilterType::comb || type == FilterType::formant)
        svf.reset();

    // The numbers agree for the four types the two enumerations share.
    if (isStateVariable (newType))
        svf.setMode (static_cast<StateVariableFilter::Mode> (static_cast<int> (newType)));

    type = newType;
}

void VoiceFilter::setCoefficients (const VoiceFilterCoefficients& newCoefficients) noexcept
{
    switch (type)
    {
        case FilterType::lowpass:
        case FilterType::highpass:
        case FilterType::bandpass:
        case FilterType::notch:
            svf.setCoefficients (newCoefficients.svf);
            break;

        case FilterType::comb:
            comb.setCoefficients (newCoefficients.comb);
            break;

        case FilterType::formant:
            formant.setCoefficients (newCoefficients.formant);
            break;

        case FilterType::off:
        default:
            break;
    }
}

float VoiceFilter::processSample (float input) noexcept
{
    switch (type)
    {
        case FilterType::lowpass:
        case FilterType::highpass:
        case FilterType::bandpass:
        case FilterType::notch:
            return svf.processSample (input);

        case FilterType::comb:
            return comb.processSample (input);

        case FilterType::formant:
            return formant.processSample (input);

        case FilterType::off:
        default:
            return input;
    }
}

} // namespace apollo::dsp
