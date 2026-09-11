/*
    The wire contract, as types.

    Every shape here has a counterpart in `Source/UI/BridgeProtocol.cpp`, and the
    point of writing them down is that the two can now disagree at compile time
    rather than at three in the morning. TypeScript cannot check the native side,
    so these are a statement of what the page believes it is being sent — but a
    statement the whole page is checked against, which is more than the untyped
    version could say (UI_BINDINGS.md §10).

    Nothing here is optional for convenience. A field is optional exactly where
    the native side omits it, and each of those omissions means something: a
    scope source that is not captured is absent rather than empty, and an
    unrouted modulator carries no trace because it has none.
*/

export const PROTOCOL_VERSION = 1;

//==============================================================================
// Parameters

/** One parameter's metadata, as `makeParameterMetadataMessage` sends it.

    This is the page's only source of truth about a parameter's range. Nothing in
    the interface may restate a minimum, a maximum, a step or a default
    (UI_BINDINGS.md §16).
*/
export interface ParameterDefinition {
    id: string;
    name: string;
    /** `params::ParameterType`'s token — "float", "int", "bool", "choice". */
    type: string;
    /** `params::ParameterUnit`'s token — "Hz", "ms", "dB", "%", and so on. */
    unit: string;
    min: number;
    max: number;
    /** Spelled `default` on the wire, which is a reserved word in neither JSON
        nor TypeScript property position — so it is left exactly as sent. */
    default: number;
    step: number;
    skew: number;
    automatable: boolean;
    modulatable: boolean;
    smoothed: boolean;
}

//==============================================================================
// Telemetry

export type ScopeSourceToken = 'output' | 'osc1' | 'osc2' | 'sub' | 'noise' | 'filter';

export interface ScopeFrame {
    source: ScopeSourceToken;
    /** Integer thousandths of full scale; divide by 1000 (UI_BINDINGS.md §12). */
    points: number[];
    peak: number;
    silent: boolean;
    triggered: boolean;
}

export type ModulatorToken =
    | 'env1' | 'env2' | 'env3' | 'env4'
    | 'lfo1' | 'lfo2' | 'lfo3' | 'lfo4';

export interface ModulatorFrame {
    source: ModulatorToken;
    /** Absent when `routed` is false: an unadvanced modulator has no trace. */
    points?: number[];
    current: number;
    routed: boolean;
    /** `dsp::EnvelopeStage`'s numeric value; meaningless for an LFO. */
    stage: number;
}

export interface WavetableFrame {
    /** 1 or 2. Named rather than positional, because entries can drop out. */
    osc: number;
    points: number[];
    position: number;
    table: number;
}

export interface MeterFrame {
    active: boolean;
    peak?: number[];
    rms?: number[];
    clipped?: boolean;
}

//==============================================================================
// Inbound messages

export interface ParameterMetadataMessage {
    type: 'parameterMetadata';
    version: number;
    parameters: ParameterDefinition[];
}

export interface StateSnapshotMessage {
    type: 'stateSnapshot';
    version: number;
    parameters: Record<string, number>;
}

export interface ParameterChangedMessage {
    type: 'parameterChanged';
    version: number;
    id: string;
    normalizedValue: number;
}

export interface ScopeFramesMessage {
    type: 'scopeFrames';
    version: number;
    scopes: ScopeFrame[];
}

export interface InstrumentFrameMessage {
    type: 'instrumentFrame';
    version: number;
    modulators: ModulatorFrame[];
    wavetables: WavetableFrame[];
    meter: MeterFrame;
    voices: number;
    polyphony: number;
}

export interface MidiMappingEntry {
    id: string;
    controller: number;
    channel: number;
    min: number;
    max: number;
}

export interface MidiMappingsMessage {
    type: 'midiMappings';
    version: number;
    mappings: MidiMappingEntry[];
    learning: string;
    capacity: number;
    status: string;
    statusMessage: string;
}

export interface ControllerProfileEntry {
    id: string;
    name: string;
    description: string;
    assignments: number;
}

export interface ControllerProfilesMessage {
    type: 'controllerProfiles';
    version: number;
    profiles: ControllerProfileEntry[];
}

export interface ErrorMessage {
    type: 'error';
    version: number;
    code: string;
    message: string;
}

export type InboundMessage =
    | ParameterMetadataMessage
    | StateSnapshotMessage
    | ParameterChangedMessage
    | ScopeFramesMessage
    | InstrumentFrameMessage
    | MidiMappingsMessage
    | ControllerProfilesMessage
    | ErrorMessage;

//==============================================================================
// Outbound messages

export type GestureState = 'begin' | 'end';

/** How an applied profile treats the mappings already in place. Absent on the
    wire means replace, which is what "set my controller up" means when it is
    said about a controller that was set up for something else. */
export type ProfileMode = 'replace' | 'merge';

export type OutboundMessage =
    | { type: 'requestState'; version: number }
    | { type: 'requestMetadata'; version: number }
    | { type: 'setParameter'; version: number; id: string; normalizedValue: number }
    | { type: 'gesture'; version: number; id: string; state: GestureState }
    | { type: 'requestMidiMappings'; version: number }
    | { type: 'midiLearnBegin'; version: number; id: string }
    | { type: 'midiLearnCancel'; version: number }
    | { type: 'midiMappingRemove'; version: number; id: string }
    | { type: 'midiMappingClearAll'; version: number }
    | { type: 'requestControllerProfiles'; version: number }
    | { type: 'applyControllerProfile'; version: number; profile: string; mode: ProfileMode };
