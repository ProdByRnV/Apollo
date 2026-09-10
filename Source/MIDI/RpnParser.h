#pragma once

/*
    Registered Parameter Number decoding.

    An RPN is not a message. It is a *sequence* of ordinary control changes —
    select the parameter with CC 101 and CC 100, then send its value with CC 6
    and optionally CC 38 — and the sequence is per channel, so a controller may
    have several in flight at once. That is the whole reason this is a class with
    state rather than a function.

    Apollo acts on exactly two of them, and ignores the rest rather than guessing:

      - **RPN 0, pitch-bend sensitivity.** The standard way a controller says how
        far its wheel is meant to bend.
      - **RPN 6, the MPE Configuration Message.** The standard way an MPE
        controller announces its zone, so that plugging one in configures Apollo
        instead of leaving the player to find a switch (CLAUDE.md §16.3).

    AUDIO THREAD. Fixed state, no allocation, no unbounded work: the whole parser
    is thirty-two integers and a switch.
*/

#include "MIDI/MpeZone.h"

#include <array>
#include <cstddef>

namespace apollo::midi
{

/** Controller numbers that carry RPN plumbing rather than a control value. */
inline constexpr int dataEntryMsbController = 6;
inline constexpr int dataEntryLsbController = 38;
inline constexpr int nrpnLsbController = 98;
inline constexpr int nrpnMsbController = 99;
inline constexpr int rpnLsbController = 100;
inline constexpr int rpnMsbController = 101;

/** The parameter number of the two RPNs Apollo understands. */
inline constexpr int pitchBendSensitivityRpn = 0;
inline constexpr int mpeConfigurationRpn = 6;

/** The "no parameter selected" value both RPN bytes take. */
inline constexpr int nullRpnByte = 127;

enum class RpnType
{
    none = 0,
    pitchBendSensitivity, ///< valueMsb is semitones, valueLsb is cents.
    mpeConfiguration      ///< valueMsb is the member channel count; 0 disables.
};

struct RpnMessage
{
    RpnType type = RpnType::none;

    /** The channel the sequence arrived on. For an MPE Configuration Message
        this is what says *which* zone: channel 1 is the lower zone, channel 16
        the upper.
    */
    int channel = 0;

    int valueMsb = 0;
    int valueLsb = 0;

    [[nodiscard]] constexpr bool isValid() const noexcept { return type != RpnType::none; }
};

//==============================================================================

class RpnParser
{
public:
    RpnParser() = default;

    /** Feeds one control-change message.

        @returns a completed RPN, or one whose type is `none` — which is the
                 common case, because most control changes are not RPN plumbing
                 at all and even those that are only complete a sequence on the
                 data-entry byte.
    */
    [[nodiscard]] RpnMessage process (int channel, int controller, int value) noexcept;

    /** Forgets every partially received sequence. */
    void reset() noexcept;

    /** @returns true if this controller is part of RPN plumbing and therefore
        carries no control value of its own.
    */
    [[nodiscard]] static constexpr bool isRpnController (int controller) noexcept
    {
        return controller == dataEntryMsbController
               || controller == dataEntryLsbController
               || controller == nrpnLsbController
               || controller == nrpnMsbController
               || controller == rpnLsbController
               || controller == rpnMsbController;
    }

private:
    struct ChannelState
    {
        /** The selected parameter number, as two 7-bit halves. 127/127 is the
            specification's "null RPN" — nothing selected — and is the correct
            state to start in: a data entry that arrives with no parameter
            selected must be ignored, not applied to parameter zero.
        */
        int rpnMsb = nullRpnByte;
        int rpnLsb = nullRpnByte;

        /** Remembered so that a CC 38 arriving after CC 6 can complete the
            value rather than reporting a semitone count of zero.
        */
        int dataMsb = 0;
    };

    [[nodiscard]] RpnType selectedType (const ChannelState& state) const noexcept;

    std::array<ChannelState, static_cast<std::size_t> (midiChannelCount)> channels {};
};

} // namespace apollo::midi
