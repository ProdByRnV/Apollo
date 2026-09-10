#pragma once

/*
    MPE zone layout, and what a MIDI channel means inside one.

    MPE (MIDI Polyphonic Expression) is ordinary MIDI with a channel convention:
    a *zone* is one **manager** channel plus a run of **member** channels, and a
    controller puts each simultaneously sounding note on its own member channel
    so that channel pitch bend, channel pressure and CC 74 — which are per-channel
    messages in plain MIDI — become per-note messages.

    That is the whole idea, and it is why Apollo does not need a separate MPE
    code path in the engine. A voice records the channel its note arrived on, and
    one rule decides what a channel message reaches:

        no zone          — every voice, exactly as plain MIDI has always behaved
        manager channel  — every voice in the zone
        member channel   — only the voices on that channel

    Two zone layouts exist, and they are fixed by the specification rather than
    chosen by Apollo:

        lower zone   manager = 1,  members = 2 … 2 + n - 1
        upper zone   manager = 16, members = 16 - n … 15

    JUCE-free, and pure data plus arithmetic, so the classification rules can be
    tested without a device or a host — which matters, because the off-by-one at
    each end of a zone is exactly the kind of mistake that shows up as one note
    in a chord not responding.
*/

namespace apollo::midi
{

/** MIDI channels are 1-16 throughout Apollo, matching JUCE's numbering. */
inline constexpr int firstMidiChannel = 1;
inline constexpr int lastMidiChannel = 16;
inline constexpr int midiChannelCount = 16;

/** The controller number MPE reserves for the third expression dimension.

    "Timbre", "slide" or the Y axis, depending on whose controller it is. It is
    an ordinary CC everywhere else, and Apollo treats it as one whenever no zone
    is active — including as a MIDI Learn target.
*/
inline constexpr int timbreController = 74;

/** Which zone is active, if any.

    The numeric values are part of the serialized contract: they are the plain
    values of the `mpe_zone` parameter.
*/
enum class MpeZoneType
{
    off = 0,
    lower,
    upper
};

/** Default pitch-bend range for the member channels of a zone, in semitones.

    ±48 is the MPE specification's default and what essentially every MPE
    controller assumes. It is deliberately enormous: a member channel's bend is
    a finger sliding across a note, not a pitch wheel.
*/
inline constexpr int defaultMemberPitchBendRange = 48;

/** Default range for the pitch wheel, in semitones. ±2 is near-universal, and
    an instrument that disagrees with every other instrument on the same MIDI
    input is simply wrong.
*/
inline constexpr int defaultPitchBendRange = 2;

//==============================================================================

struct MpeZone
{
    MpeZoneType type = MpeZoneType::off;

    /** How many member channels the zone claims, 1-15. */
    int memberCount = 15;

    [[nodiscard]] constexpr bool isActive() const noexcept
    {
        return type != MpeZoneType::off && memberCount >= 1 && memberCount <= 15;
    }

    /** @returns the manager channel, or 0 when no zone is active. */
    [[nodiscard]] constexpr int managerChannel() const noexcept
    {
        if (! isActive()) return 0;

        return type == MpeZoneType::lower ? firstMidiChannel : lastMidiChannel;
    }

    [[nodiscard]] constexpr int firstMemberChannel() const noexcept
    {
        if (! isActive()) return 0;

        return type == MpeZoneType::lower ? firstMidiChannel + 1
                                          : lastMidiChannel - memberCount;
    }

    [[nodiscard]] constexpr int lastMemberChannel() const noexcept
    {
        if (! isActive()) return 0;

        return type == MpeZoneType::lower ? firstMidiChannel + memberCount
                                          : lastMidiChannel - 1;
    }

    [[nodiscard]] constexpr bool isManagerChannel (int channel) const noexcept
    {
        return isActive() && channel == managerChannel();
    }

    [[nodiscard]] constexpr bool isMemberChannel (int channel) const noexcept
    {
        return isActive() && channel >= firstMemberChannel() && channel <= lastMemberChannel();
    }

    /** @returns true if a channel-wide message arriving on @p messageChannel
        should reach a voice whose note arrived on @p voiceChannel.

        The one rule the whole feature reduces to. With no zone active it is
        always true, which is exactly how Apollo behaved before MPE existed: a
        pitch wheel moves every sounding note.
    */
    [[nodiscard]] constexpr bool appliesTo (int messageChannel, int voiceChannel) const noexcept
    {
        if (! isActive())
            return true;

        if (isManagerChannel (messageChannel))
            return true;

        if (isMemberChannel (messageChannel))
            return messageChannel == voiceChannel;

        // A channel outside the zone belongs to nothing Apollo is playing.
        return false;
    }

    [[nodiscard]] constexpr bool operator== (const MpeZone& other) const noexcept
    {
        return type == other.type && memberCount == other.memberCount;
    }

    [[nodiscard]] constexpr bool operator!= (const MpeZone& other) const noexcept
    {
        return ! (*this == other);
    }
};

} // namespace apollo::midi
