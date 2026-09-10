#include "MIDI/RpnParser.h"

namespace apollo::midi
{

void RpnParser::reset() noexcept
{
    channels.fill (ChannelState {});
}

RpnType RpnParser::selectedType (const ChannelState& state) const noexcept
{
    // Every RPN Apollo understands lives in bank 0.
    if (state.rpnMsb != 0)
        return RpnType::none;

    if (state.rpnLsb == pitchBendSensitivityRpn)
        return RpnType::pitchBendSensitivity;

    if (state.rpnLsb == mpeConfigurationRpn)
        return RpnType::mpeConfiguration;

    return RpnType::none;
}

RpnMessage RpnParser::process (int channel, int controller, int value) noexcept
{
    // AUDIO THREAD.
    if (channel < firstMidiChannel || channel > lastMidiChannel)
        return {};

    auto& state = channels[static_cast<std::size_t> (channel - firstMidiChannel)];

    switch (controller)
    {
        case rpnMsbController:
            state.rpnMsb = value;
            return {};

        case rpnLsbController:
            state.rpnLsb = value;
            return {};

        case nrpnMsbController:
        case nrpnLsbController:
            // A non-registered parameter is being selected. Apollo understands
            // none of them, but it must stop treating a following data entry as
            // belonging to whichever RPN was selected before — which is what
            // returning to the null selection does.
            state.rpnMsb = nullRpnByte;
            state.rpnLsb = nullRpnByte;
            return {};

        case dataEntryMsbController:
        {
            state.dataMsb = value;

            RpnMessage message;
            message.type = selectedType (state);
            message.channel = channel;
            message.valueMsb = value;
            message.valueLsb = 0;
            return message;
        }

        case dataEntryLsbController:
        {
            // The fine half. Reported as a complete message with the remembered
            // coarse half, so a controller that sends both produces one usable
            // value rather than a semitone count followed by a cent count with
            // no semitones.
            RpnMessage message;
            message.type = selectedType (state);
            message.channel = channel;
            message.valueMsb = state.dataMsb;
            message.valueLsb = value;
            return message;
        }

        default:
            break;
    }

    return {};
}

} // namespace apollo::midi
