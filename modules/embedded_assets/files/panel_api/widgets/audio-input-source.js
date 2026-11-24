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

import { el, setPressed } from "../core/dom.js";
import { EndpointWidget } from "./widget-base.js";
import { addLevelDisplay } from "./audio-meter.js";
import { SignalGeneratorForm, ThrottledSender, normaliseGeneratorSettings, mergeGeneratorSettings, defaultGeneratorSettings,
         loadSavedGeneratorSettings, saveGeneratorSettings, getGeneratorTypeInfo } from "./signal-generator.js";

const maxUploadFileSize = 1024 * 1024 * 50;

/// How long to wait for a server to show that it can play test signals: after its first reply
/// about the input mode (to a request for the generator settings, which it answers straight
/// after that), or after asking it to play one
export const generatorReplyTimeoutMs = 2000;

/// True if a drag carries files (rather than e.g. some text, or an element of the page)
const isFileDrag = (e) => [...(e.dataTransfer?.types ?? [])].includes ("Files");

/*
    An audio input: chooses where its audio comes from (the live input device, a test signal
    that the server generates, a looped audio file, or silence), and shows its level and scope.
    An audio file can also be dropped onto it. The compact one is just the level meter (and still
    takes a dropped file); the full one in a popover has the rest.

    In Signal mode, the full widget shows a SignalGeneratorForm. It follows the server's
    audio_input_generator_<endpoint> reports while the input plays a generator (so another client's
    changes show up here), and sends only the fields the user changes. The last settings are saved
    per patch, and sent in full when Signal is chosen, so a server that has forgotten them (it was
    restarted) still gets them back.

    An older server knows nothing of generators: it doesn't answer req_audio_input_generator, and
    takes a generator request as "live". If no answer comes within generatorReplyTimeoutMs of its
    first mode reply (or the mode doesn't become "generator" that long after choosing Signal), the
    Signal button is disabled, with a note saying why. Nothing else changes.
*/
export class AudioInputWidget  extends EndpointWidget
{
    constructor (endpoint, options)
    {
        super (endpoint, options, "audio-in");

        this.session = options.session;
        this.settings = options.settings ?? null;
        this.mode = "live";

        const id = endpoint.endpointID;
        const canGenerate = typeof this.session?.setAudioInputGenerator === "function";
        this.generatorSettings = loadSavedGeneratorSettings (this.settings, id) ?? { ...defaultGeneratorSettings };
        this.hasSavedGeneratorSettings = loadSavedGeneratorSettings (this.settings, id) !== null;

        // (the compact one has no source buttons, so it says when it isn't live)
        addLevelDisplay (this, (text) => this.formatLevelText (text));

        this.fileInput = el ("input", { type: "file", accept: "audio/*,.wav,.aif,.aiff,.flac,.ogg,.mp3", hidden: true,
                                        onchange: () => { this.useFile (this.fileInput.files[0]); this.fileInput.value = ""; } });

        const makeButton = (mode, label, title, onclick) =>
            el ("button", { class: "cp-seg-button", dataset: { mode }, title, "aria-pressed": "false", onclick }, label);

        this.buttons = [
            makeButton ("live", "Live", "Use the audio input device", () => this.setSource (false)),
            canGenerate ? makeButton ("generator", "Signal", "Play a test signal: a sine, sweep, noise or impulses", () => this.useGenerator()) : null,
            makeButton ("file", "File…", "Loop an audio file (or drop one here)", () => this.fileInput.click()),
            makeButton ("mute", "Mute", "Silence this input", () => this.setSource (true)),
        ].filter (Boolean);

        this.generatorButton = this.buttons.find (b => b.dataset.mode === "generator") ?? null;
        this.bodyElement.append (this.fileInput);

        if (! this.isCompact)
        {
            const sourceButtons = el ("div", { class: "cp-seg", role: "group", "aria-label": "Input source" }, this.buttons);
            this.display.element.after (sourceButtons);

            if (canGenerate)
            {
                this.generatorForm = new SignalGeneratorForm ({ label: "Test signal settings", onChange: (changes) => this.changeGenerator (changes) });
                this.generatorForm.setSettings (this.generatorSettings);
                this.generatorForm.element.hidden = true;
                this.generatorNote = el ("p", { class: "cp-ew-note", role: "status", hidden: true });
                sourceButtons.after (this.generatorForm.element, this.generatorNote);
                this.disposer.add (() => this.generatorForm.dispose());
            }
        }

        if (canGenerate)
        {
            // (a change that's still waiting when the input has been switched to something else
            // mustn't switch it back)
            this.sender = new ThrottledSender ((changes) => { if (this.mode === "generator") this.session?.setAudioInputGenerator (id, changes); },
                                               { scheduler: this.scheduler });
            this.disposer.add (() => this.sender.flush());
            this.disposer.add (() => clearTimeout (this.supportTimer));
            this.disposer.listen (supportChanges, "change", (e) => { if (e.detail === this.session) this.showGeneratorSupport(); });
            this.showGeneratorSupport();

            if (options.store)
                this.disposer.add (options.store.subscribe (["patchSettings"], () => this.reloadSavedSettings()));
        }

        if (this.session)
        {
            const modeListener = (mode) => this.handleModeFromServer (mode);
            this.session.addAudioInputModeListener (id, modeListener);
            this.disposer.add (() => this.session.removeAudioInputModeListener (id, modeListener));

            if (canGenerate)
            {
                const generatorListener = (settings) => this.handleGeneratorFromServer (settings);
                this.session.addAudioInputGeneratorListener (id, generatorListener);
                this.disposer.add (() => this.session.removeAudioInputGeneratorListener (id, generatorListener));
            }

            this.session.requestAudioInputMode (id);

            if (canGenerate)
                this.session.requestAudioInputGenerator (id);
        }

        // A file dropped on it is looped as its input. (The enter/leave events are counted
        // because they also arrive as the pointer crosses the widget's children, which would
        // make the highlight flicker.)
        let dragDepth = 0;
        const setDropTarget = (isTarget) => this.element.classList.toggle ("cp-drop-target", isTarget);

        this.disposer.listen (this.element, "dragenter", e => { if (isFileDrag (e)) { e.preventDefault(); ++dragDepth; setDropTarget (true); } });
        this.disposer.listen (this.element, "dragover",  e => { if (isFileDrag (e)) e.preventDefault(); });
        this.disposer.listen (this.element, "dragleave", e => { if (isFileDrag (e) && --dragDepth <= 0) { dragDepth = 0; setDropTarget (false); } });

        this.disposer.listen (this.element, "drop", e =>
        {
            dragDepth = 0;
            setDropTarget (false);

            if (! isFileDrag (e))
                return;

            e.preventDefault();
            e.stopPropagation();
            this.useFile (e.dataTransfer?.files?.[0]);
        });

        liveAudioInputs.add (this);
        this.disposer.add (() => liveAudioInputs.delete (this));

        this.updateMode ("live");
    }

    /// The header's text: the level, and in the compact widget (which has no source buttons),
    /// where the input comes from when it isn't the live one
    formatLevelText (text)
    {
        if (! this.isCompact || this.mode === "live")
            return text;

        if (this.mode === "mute")
            return "muted";

        if (this.mode === "generator")
            return this.generatorSettings.type === "silence" ? "silence" : `${getGeneratorTypeInfo (this.generatorSettings.type).shortName} ${text}`;

        return "file " + text;
    }

    updateMode (mode)
    {
        this.mode = mode;
        this.element.dataset.inputMode = mode;

        for (const b of this.buttons)
            setPressed (b, b.dataset.mode === mode);

        if (this.generatorForm)
            this.generatorForm.element.hidden = mode !== "generator";

        if (mode === "mute")
            this.display.clear();
    }

    setSource (shouldMute, fileData)
    {
        this.sender?.cancel();
        this.session?.setAudioInputSource (this.endpoint.endpointID, shouldMute, fileData);
    }

    //==============================================================================
    /// Switches the input to the test signal, with the last settings it had
    useGenerator()
    {
        if (! this.sender || getGeneratorSupport (this.session) === false)
            return;

        this.sender.cancel();
        this.session.setAudioInputGenerator (this.endpoint.endpointID, { ...this.generatorSettings });

        // (an older server takes this as a request for the live input)
        if (this.mode !== "generator")
            this.startSupportTimer (true);
    }

    /// The user changed some fields in the form: they're sent (throttled), and saved
    changeGenerator (changes)
    {
        this.generatorSettings = mergeGeneratorSettings (this.generatorSettings, changes);
        this.sender.queue (changes);
        this.saveSettings();
    }

    saveSettings()
    {
        saveGeneratorSettings (this.settings, this.endpoint.endpointID, this.generatorSettings);
        this.hasSavedGeneratorSettings = !! this.settings?.current;
    }

    reloadSavedSettings()
    {
        const saved = loadSavedGeneratorSettings (this.settings, this.endpoint.endpointID);
        this.hasSavedGeneratorSettings = saved !== null;

        if (saved && this.mode !== "generator")
            this.showGeneratorSettings (saved);
    }

    showGeneratorSettings (settings)
    {
        this.generatorSettings = normaliseGeneratorSettings (settings);
        this.generatorForm?.setSettings (this.generatorSettings);
    }

    handleModeFromServer (mode)
    {
        this.updateMode (mode);

        if (mode === "generator")
            setGeneratorSupport (this.session, true);
        else if (this.sender && getGeneratorSupport (this.session) === undefined && ! this.supportTimer)
            this.startSupportTimer (false);
    }

    /// The server's settings are the truth while the input plays a generator (another client may
    /// have changed them). Otherwise they're the ones it remembers for next time, and the ones
    /// saved for the patch take precedence, as the server may have been restarted since.
    handleGeneratorFromServer (settings)
    {
        setGeneratorSupport (this.session, true);

        if (this.mode === "generator")
        {
            this.showGeneratorSettings (settings);
            this.saveSettings();
        }
        else if (! this.hasSavedGeneratorSettings)
        {
            this.showGeneratorSettings (settings);
        }
    }

    startSupportTimer (wasRequestedByUser)
    {
        clearTimeout (this.supportTimer);

        this.supportTimer = setTimeout (() =>
        {
            this.supportTimer = null;

            if (getGeneratorSupport (this.session) !== true && this.mode !== "generator")
            {
                setGeneratorSupport (this.session, false);
                this.wasSupportRequestedByUser = wasRequestedByUser || this.wasSupportRequestedByUser;
                this.showGeneratorSupport();
            }
        }, this.options.generatorReplyTimeoutMs ?? generatorReplyTimeoutMs);
    }

    showGeneratorSupport()
    {
        const isUnsupported = getGeneratorSupport (this.session) === false;

        if (this.generatorButton)
        {
            this.generatorButton.disabled = isUnsupported;
            this.generatorButton.title = isUnsupported ? "This Cmajor server is too old to play test signals"
                                                       : "Play a test signal: a sine, sweep, noise or impulses";
        }

        if (this.generatorNote)
        {
            this.generatorNote.textContent = "This Cmajor server can't play test signals: it's older than this panel.";
            this.generatorNote.hidden = ! (isUnsupported && this.wasSupportRequestedByUser);
        }
    }

    //==============================================================================
    useFile (file)
    {
        if (! file)
            return;

        if (file.size > maxUploadFileSize)
        {
            this.reportError (`${file.name} is too big to use as an input (the limit is 50MB)`);
            return;
        }

        file.arrayBuffer().then (data => this.setSource (false, new Uint8Array (data)),
                                 error => this.reportError (`Couldn't read ${file.name}: ${error?.message ?? error}`));
    }

    /// (a compact widget has no bridge, so it can't show an alert: it goes to the console instead)
    reportError (message)
    {
        if (this.options.bridge)
            this.options.bridge.showErrorAlert (message);
        else
            this.options.log?.("error", "panel", message);
    }
}

//==============================================================================
// Whether each session's server can play test signals: true once it has sent any generator
// settings, false once it has failed to answer in time, and undefined until then. (A server
// that answers later is believed again.)

const generatorSupport = new WeakMap();
const supportChanges = new EventTarget();

export function getGeneratorSupport (session)
{
    return session ? generatorSupport.get (session) : undefined;
}

function setGeneratorSupport (session, isSupported)
{
    if (session && generatorSupport.get (session) !== isSupported)
    {
        generatorSupport.set (session, isSupported);
        supportChanges.dispatchEvent (new CustomEvent ("change", { detail: session }));
    }
}

/// The AudioInputWidgets that exist now, so that the commands can reach them
const liveAudioInputs = new Set();

export function findAudioInputWidgets (session, endpointID)
{
    return [...liveAudioInputs].filter (w => w.session === session && w.endpoint.endpointID === endpointID);
}

//==============================================================================
/*
    The audio input commands for the palette: a test signal on the first audio input (with its
    last settings), and back to the live input. Disabled when the patch has no audio input (and
    the test signal, when the server is known to be too old for one).

    getAudioInputs(): the patch's audio input endpoints; getSession(); settings: the panel's
    PanelSettingsScopes
*/
export function createAudioInputCommands ({ getAudioInputs, getSession, settings })
{
    const getFirstInputID = () => getSession() ? getAudioInputs()[0]?.endpointID : undefined;

    const useGenerator = () =>
    {
        const session = getSession();
        const id = getFirstInputID();
        const widgets = findAudioInputWidgets (session, id);

        if (widgets.length > 0)
            widgets[0].useGenerator();
        else if (id)
            session.setAudioInputGenerator (id, loadSavedGeneratorSettings (settings, id) ?? {});
    };

    const useLiveInput = () =>
    {
        const id = getFirstInputID();

        if (id)
            getSession().setAudioInputSource (id, false);
    };

    return [
        { id: "audio.useTestSignal", title: "Use a test signal on the audio input", category: "Audio",
          keywords: ["generator", "sine", "sweep", "noise", "impulse", "tone", "oscillator"],
          run: useGenerator,
          isEnabled: () => !! getFirstInputID() && typeof getSession().setAudioInputGenerator === "function" && getGeneratorSupport (getSession()) !== false },
        { id: "audio.useLiveInput", title: "Use the live audio input", category: "Audio", keywords: ["microphone", "device", "line in"],
          run: useLiveInput, isEnabled: () => !! getFirstInputID() },
    ];
}
