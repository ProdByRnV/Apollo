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
    /** The matrix destination this control is, or 0 (Phase 13h-4): which
        knobs take a dropped source, and which one a routing lights. */
    destination: number;
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

    /** The rate the engine is prepared at, or 0 before it has been. Carried
        here because the equaliser's curve is designed with it. */
    sampleRate: number;
}

/** The equaliser's input and output spectra (CLAUDE.md §22), drawn behind
    its curve. Levels are integer tenths of a decibel, one per point on a
    logarithmic axis from 20 Hz to 20 kHz; divide by 10. Absent when `active` is
    false, which means there is no equaliser in the rack rather than that it is
    quiet. */
export interface SpectrumFrameMessage {
    type: 'spectrumFrame';
    version: number;
    active: boolean;
    silent: boolean;
    input?: number[];
    output?: number[];
}

/** One point of a drawn LFO curve: position in the cycle (0 to 1), value
    (-1 to +1), and the bend of the segment leaving it (-1 to +1). */
export type LfoCurvePoint = [number, number, number];

/** Every LFO's drawn curve, all four, as the store holds them (Phase 13f-1).
    Sent on request, in answer to every curve the page sets, and after a load. */
export interface LfoCurvesMessage {
    type: 'lfoCurves';
    version: number;
    curves: { lfo: number; points: LfoCurvePoint[] }[];
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
    /** A built-in's permanent id, or `user-N`, numbered by the last scan. */
    id: string;
    name: string;
    description: string;
    assignments: number;
    /** True for one of the user's, which can be replaced and removed. */
    user: boolean;
}

export interface ControllerProfilesMessage {
    type: 'controllerProfiles';
    version: number;
    profiles: ControllerProfileEntry[];
    /** Since 13h-3. Files in the user's folder that could not be read. */
    unreadable?: number;
    /** SAVED, ALREADY_EXISTS, SAVE_FAILED, DELETED, DELETE_FAILED, EXPORTED,
        EXPORT_FAILED, IMPORTED, IMPORT_FAILED, or empty for the list alone. */
    status?: string;
    statusMessage?: string;
    /** The profile the picker should now show, or empty. */
    selected?: string;
}

/** One preset as the browser lists it.

    No path, and that is not an oversight: the backend numbers what it found and
    the page asks for a number, so there is nothing here that could be turned
    back into a location on disk (UI_BINDINGS.md §13).
*/
export interface PresetEntry {
    /** What to send back to load it. Valid only against the index it came in. */
    id: number;
    name: string;
    author: string;
    category: string;
    /** The folder it sits in, relative to its root, or "" at the top. */
    bank: string;
    factory: boolean;
}

export interface PresetIndexMessage {
    type: 'presetIndex';
    version: number;
    presets: PresetEntry[];
    /** Files carrying the extension that could not be read as presets. */
    unreadable: number;
    /** True when the scan stopped early; what is listed is still valid. */
    truncated: boolean;
    userMissing: boolean;
    factoryMissing: boolean;
    /** True while a scan is running, so the list can say it is still filling. */
    scanning: boolean;
}

export interface PresetStatusMessage {
    type: 'presetStatus';
    version: number;
    /** "LOADED", "SAVED", "ALREADY_EXISTS", "LOAD_FAILED", "SAVE_FAILED",
        "STATE_RELOADED", and since 13h-2 "PROGRAM_EMPTY" for a program change
        that named nothing — branched on, so they are part of the contract. */
    status: string;
    statusMessage: string;
    name: string;
    author: string;
    category: string;
    comment: string;
    /** The id of the preset currently loaded, or 0 for a sound that did not
        come from the library. */
    loaded: number;
}

/** Where the user's own content can go (Phase 13g). */
export type ContentSlot = 'osc1' | 'osc2' | 'noise';
export type ContentKind = 'wavetable' | 'noise';

export interface ContentEntry {
    /** What to send back to load it. Valid only against the index it came in. */
    id: number;
    name: string;
    /** The folder it sits in, relative to its root, or "" at the top. */
    bank: string;
    factory: boolean;
    /** A wavetable's frames, from its header. */
    frames?: number;
    /** A sample's length in seconds, from its header. */
    seconds?: number;
    /** Why its header says it will not load, or "". */
    problem: string;
}

export interface ContentIndexMessage {
    type: 'contentIndex';
    version: number;
    wavetables: ContentEntry[];
    noise: ContentEntry[];
    truncated: boolean;
    scanning: boolean;
}

export interface ContentSlotState {
    slot: ContentSlot;
    loaded: boolean;
    name: string;
    /** "library", "factory" or "file". */
    origin: string;
    frames: number;
    seconds: number;
    /** The library entry it came from, if it is still listed; else 0. */
    entry: number;
    loading: boolean;
    loadingName: string;
}

export interface ContentSlotsMessage {
    type: 'contentSlots';
    version: number;
    slots: ContentSlotState[];
    /** LOADED, LOAD_FAILED, RESTORED, RESTORE_FAILED, CLEARED, LOADING, SAVED,
        SAVE_FAILED, or CURRENT when nothing happened. */
    status: string;
    /** The slot it happened to, or "". */
    slot: string;
    statusMessage: string;
}

/** What a dropped source or a cleared slot did (Phase 13h-4). The slots are
    parameters and arrive as parameters do; this is the sentence. */
export interface ModulationRoutingMessage {
    type: 'modulationRouting';
    version: number;
    /** ASSIGNED, ALREADY_ROUTED, MATRIX_FULL or CLEARED. */
    status: string;
    statusMessage: string;
    /** From 1, or 0 for none. */
    slot: number;
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
    | SpectrumFrameMessage
    | LfoCurvesMessage
    | MidiMappingsMessage
    | ControllerProfilesMessage
    | PresetIndexMessage
    | PresetStatusMessage
    | ContentIndexMessage
    | ContentSlotsMessage
    | ModulationRoutingMessage
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
    | { type: 'toggleFullscreen'; version: number }
    | { type: 'applyControllerProfile'; version: number; profile: string; mode: ProfileMode }
    | { type: 'saveControllerProfile'; version: number; name: string; description: string; overwrite: boolean }
    | { type: 'deleteControllerProfile'; version: number; profile: string }
    | { type: 'exportControllerProfile'; version: number; profile: string }
    | { type: 'importControllerProfile'; version: number }
    | {
          type: 'midiMappingEdit';
          version: number;
          id: string;
          controller: number;
          /** 0 is any channel. */
          channel: number;
          min: number;
          max: number;
      }
    | { type: 'requestPresets'; version: number }
    | { type: 'rescanPresets'; version: number }
    | { type: 'loadPreset'; version: number; preset: number }
    | { type: 'requestLfoCurves'; version: number }
    | { type: 'requestContent'; version: number }
    | { type: 'rescanContent'; version: number }
    | { type: 'loadContent'; version: number; slot: ContentSlot; entry: number }
    | { type: 'chooseContentFile'; version: number; slot: ContentSlot }
    | { type: 'clearContent'; version: number; slot: ContentSlot }
    | { type: 'saveContentToLibrary'; version: number; slot: ContentSlot }
    | { type: 'revealContentFolder'; version: number; kind: ContentKind }
    | { type: 'setLfoCurve'; version: number; lfo: number; points: LfoCurvePoint[] }
    | { type: 'modulationAssign'; version: number; source: number; id: string }
    | { type: 'modulationClear'; version: number; slot: number }
    | {
          type: 'savePreset';
          version: number;
          name: string;
          author: string;
          category: string;
          comment: string;
          bank: string;
          /** Absent means no. A save that would destroy a preset already there
              has to be asked for in as many words. */
          overwrite: boolean;
      };
