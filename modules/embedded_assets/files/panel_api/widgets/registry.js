//
//     ,ad888ba,                              88
//    d8"'    "8b
//   d8            88,dba,,adba,   ,aPP8A.A8  88     The Cmajor Toolkit
//   Y8,           88    88    88  88     88  88
//    Y8a.   .a8P  88    88    88  88,   ,88  88     (C)2024 Cmajor Software Ltd
//     '"Y888Y"'   88    88    88  '"8bbP"Y8  88     https://cmajor.dev
//                                           ,88
//                                        888P"
//
//  The Cmajor project is subject to commercial or open-source licensing.
//  You may use it under the terms of the GPLv3 (see www.gnu.org/licenses), or
//  visit https://cmajor.dev to learn about our commercial licence options.
//
//  CMAJOR IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
//  EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
//  DISCLAIMED.

import { AudioOutputWidget } from "./audio-meter.js";
import { AudioInputWidget } from "./audio-input-source.js";
import { MIDIInputWidget, MIDIActivityWidget } from "./midi-input.js";
import { EventMonitorWidget, UnobservableWidget } from "./event-monitor.js";
import { ValueControlWidget } from "./value-control.js";

export { getEndpointDisplayName, getEndpointKind, getEndpointTypeDescription } from "./widget-base.js";

/// Endpoint purposes that the host provides, which get no widget (the top bar's transport
/// control shows when a patch has inputs with these)
export const hostTimelinePurposes = new Set (["time signature", "tempo", "transport state", "timeline position"]);

/*
    The one place that decides which widget shows an endpoint. The I/O view, the inspector and
    the graph's live mode all create their widgets here.

    endpoint: an entry from status.details.inputs / outputs
    options: { patchConnection, session, group, bridge, size: "full" | "compact", scheduler,
               settings (the panel's PanelSettingsScopes), store, log (level, source, text), onShowInConsole (endpointID), onValueChanged (endpoint, value),
               onUserGesture (endpointID) }
    (createWidgetOptions() makes these for a view.)
    isInput: whether it came from the inputs list

    Returns a widget (with .element and .dispose()), or null if the endpoint gets none.
*/
export function createEndpointWidget (endpoint, options, isInput)
{
    const WidgetClass = getWidgetClass (endpoint, isInput, options?.size);
    return WidgetClass ? new WidgetClass (endpoint, options) : null;
}

export function getWidgetClass (endpoint, isInput, size = "full")
{
    const purpose = endpoint?.purpose ?? "";

    if (hostTimelinePurposes.has (purpose))
        return null;

    if (purpose === "audio out")        return AudioOutputWidget;
    if (purpose === "audio in")         return AudioInputWidget;
    if (purpose === "midi in")          return MIDIInputWidget;
    if (purpose === "parameter")        return ValueControlWidget;

    // (a MIDI output's full widget is the event log; the compact one shows the notes)
    if (purpose === "midi out" && size === "compact")
        return MIDIActivityWidget;

    if (endpoint?.endpointType === "event")
        return EventMonitorWidget;

    // a stream that isn't audio-shaped, or a value output: neither can be listened to
    return isInput ? null : UnobservableWidget;
}

/// True if the endpoint gets a live widget in the graph: one with something to show or to
/// play (a value output can't be listened to, so it has neither)
export function hasLiveWidget (endpoint, isInput)
{
    const WidgetClass = getWidgetClass (endpoint, isInput, "compact");
    return WidgetClass !== null && WidgetClass !== UnobservableWidget;
}

/// True for endpoints whose data the console should log
export function isLoggableEndpoint (endpoint)
{
    const purpose = endpoint?.purpose ?? "";
    return endpoint?.endpointType === "event" && purpose !== "parameter" && ! hostTimelinePurposes.has (purpose);
}

//==============================================================================
/// Every endpoint in a store state (the inputs, then the outputs)
export function getAllEndpoints (state)
{
    return [...(state?.inputs ?? []), ...(state?.outputs ?? [])];
}

/// The parameters among a store state's inputs (including the ones the patch's GUI hides)
export function getParameters (state)
{
    return (state?.inputs ?? []).filter (e => e.purpose === "parameter");
}

/// The total number of audio channels of the endpoints with the given purpose ("audio in" or "audio out")
export function countAudioChannels (endpoints, purpose)
{
    return (endpoints ?? []).filter (e => e.purpose === purpose).reduce ((n, e) => n + (e.numAudioChannels ?? 0), 0);
}

/// The options that a view passes to createEndpointWidget() for a connection (extra ones
/// override these)
export function createWidgetOptions (ctx, connection, group, extra = {})
{
    return {
        patchConnection: connection.patchConnection,
        session: ctx.session,
        group,
        bridge: ctx.bridge,
        log: ctx.log,
        settings: ctx.settings,
        store: ctx.store,
        onShowInConsole: (endpointID) => ctx.actions.showConsole (endpointID),
        // (a user grabbing a parameter stops the fuzzer's automation of it first: see core/parameter-fuzzer.js)
        onUserGesture: (endpointID) => connection.fuzzer?.handleUserGesture (endpointID),
        ...extra,
    };
}
