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

import * as cmajor from "/cmaj-patch-server.js";
import { el, Disposer, setPressed } from "./core/dom.js";
import { HostBridge } from "./core/host-bridge.js";
import { Store, applySessionStatus, isTransientStatus } from "./core/store.js";
import { EndpointDataHub } from "./core/endpoint-data-hub.js";
import { frameScheduler } from "./core/frame-scheduler.js";
import { PanelSettingsScopes } from "./core/persistence.js";
import { applyHostTheme, applyBrowserTheme } from "./core/theme.js";
import { CommandRegistry } from "./core/commands.js";
import { TopBar } from "./shell/top-bar.js";
import { CommandPalette } from "./shell/command-palette.js";
import { Workspace } from "./shell/workspace.js";
import { ProblemsView, parseDiagnostics, countErrors } from "./shell/diagnostics.js";
import { FloatingGUIWindow } from "./shell/float-window.js";
import { PatchGUIView } from "./views/gui-view.js";
import { GraphView } from "./views/graph-view.js";
import { IOView } from "./views/io-view.js";
import { ConsoleView } from "./views/console-view.js";
import { CodeView } from "./views/code-view.js";
import { SettingsView } from "./views/settings-view.js";
import { InspectorView } from "./views/inspector-view.js";
import { ChooserView, registerDroppedPatchFiles } from "./views/chooser-view.js";
import { getEndpointDisplayName, getParameters } from "./widgets/registry.js";
import { ParameterFuzzer, createFuzzCommands } from "./core/parameter-fuzzer.js";
import { createMIDICommands } from "./widgets/midi-controls.js";
import { createAudioInputCommands } from "./widgets/audio-input-source.js";
import { createSpectrumCommandProvider } from "./widgets/spectrum-analyser.js";
import { createTransportCommands } from "./shell/transport-control.js";
import { RecordingManager } from "./core/recorder.js";
import { RecordingController, RecordingsView, CaptureButton, createRecordingCommands, normaliseCapMB, recordingSettingsKeys,
         defaultRecordingCapMB } from "./views/recordings-view.js";

/*
    <cmaj-patch-panel session-id="..." fixed-patch="true">

    The page that `cmaj server` serves at / (and that the VS Code extension shows in an iframe).
    This element is a thin shell: it owns the server session, a Store of the session's state,
    the HostBridge (VS Code or browser), and the EndpointDataHub for the loaded patch, and wires
    them to the top bar, the workspace's views, and the floating GUI window.

    With fixed-patch, the patch to show comes from VS Code ({ patchToLoad } messages, through
    handleMessageFromVSCode()); without it, the panel offers the server's patches to choose from.
*/
export default class PatchPanel  extends HTMLElement
{
    constructor()
    {
        super();

        this.session = cmajor.createServerSession (this.getSessionID());
        this.bridge = new HostBridge();
        this.store = new Store ({ connected: false, loaded: false, playing: false, connection: null, guiGeneration: 0,
                                  guiViewType: "", diagnostics: [], selection: null, deviceProperties: null,
                                  availablePatches: [], hostTheme: false, patchSettings: null, alwaysShowTransport: false });
        this.disposer = new Disposer();
        this.settings = new PanelSettingsScopes();
        this.resourceVersion = 0;
        this.hasRequestedPatchList = false;
        this.isBuilt = false;
    }

    static get observedAttributes()     { return ["session-id", "fixed-patch"]; }

    get isFixedPatch()                  { return !! this.getAttribute ("fixed-patch"); }

    getSessionID()
    {
        let sessionID = this.getAttribute ("session-id");

        if (! sessionID)
        {
            sessionID = "";

            for (let i = 0; i < 3; ++i)
                sessionID += Math.floor (Math.random() * 65536).toString (16);
        }

        return sessionID;
    }

    connectedCallback()
    {
        if (! this.isBuilt)
            this.build();
    }

    disconnectedCallback()
    {
        this.unloadPatch();
    }

    dispose()
    {
        this.unloadPatch();
        this.disposer.dispose();
        this.session.dispose();
    }

    //==============================================================================
    build()
    {
        this.isBuilt = true;
        addStyleSheets();

        this.ctx = {
            session: this.session,
            store: this.store,
            bridge: this.bridge,
            scheduler: frameScheduler,
            isFixedPatch: this.isFixedPatch,
            settings: this.settings,
            // (goes up each time the patch's files change: see addVersionFolder())
            getResourceVersion: () => this.resourceVersion,
            log: (level, source, text) => this.consoleView?.add (source, text, level),
            actions: this.createActions(),
            commands: new CommandRegistry(),
        };

        this.ctx.commands.recent = this.settings.global.get ("recentCommands", []);

        // recordings: one manager per panel, kept across patch changes, subscribed through the hub
        // itself (not a view's group) so that capturing carries on while the views are hidden
        this.recordings = this.ctx.recordings = new RecordingManager ({
            capBytes: normaliseCapMB (this.settings.global.get (recordingSettingsKeys.capMB, defaultRecordingCapMB)) * 1024 * 1024,
            scheduleNotification: fn => frameScheduler.schedule (() => { fn(); return false; }) });
        this.recordingController = this.ctx.recordingController = new RecordingController (this.ctx, this.recordings);
        this.disposer.add (this.store.subscribe (["connection"], (s) => this.recordings.handleConnectionChanged (s.connection)),
                           () => this.recordings.dispose());
        this.store.set ({ alwaysShowTransport: !! this.settings.global.get ("alwaysShowTransport", false) });

        this.guiView = new PatchGUIView (this.ctx);
        this.guiView.toolbar = this.createGUIToolbar();
        this.graphView = new GraphView (this.ctx);
        this.ioView = new IOView (this.ctx);
        this.consoleView = new ConsoleView (this.ctx);
        this.problemsView = new ProblemsView (this.ctx);
        this.codeView = new CodeView (this.ctx);
        this.settingsView = new SettingsView (this.ctx);
        this.inspectorView = new InspectorView (this.ctx);
        this.chooserView = new ChooserView (this.ctx);
        this.recordingsView = new RecordingsView (this.ctx, { manager: this.recordings, controller: this.recordingController });

        this.floatButton = el ("button", { class: "cp-button", title: "Open another instance of the patch's GUI in a floating window (G)",
                                           "aria-pressed": "false", onclick: () => this.ctx.actions.setFloatingGUIOpen (! this.floatWindow.isOpen) },
                               "⧉", el ("span", { class: "cp-button-label" }, "Float GUI"), el ("kbd", null, "G"));

        this.workspace = new Workspace (this.ctx, {
            mainTabs: [{ id: "gui", label: "GUI", key: "1", view: this.guiView },
                       { id: "graph", label: "Graph", key: "2", view: this.graphView },
                       { id: "io", label: "I/O", key: "3", view: this.ioView }],
            mainTools: [this.floatButton],
            inspector: this.inspectorView,
            drawerTabs: [{ id: "console", label: "Console", view: this.consoleView },
                         { id: "problems", label: "Problems", view: this.problemsView },
                         { id: "recordings", label: "Recordings", view: this.recordingsView },
                         { id: "code", label: "Generated code", view: this.codeView }]
        });

        this.captureButton = new CaptureButton (this.ctx, this.recordingController);
        this.topBar = new TopBar (this.ctx, this.settingsView, { captureButton: this.captureButton });
        this.disposer.add (() => this.captureButton.dispose());
        this.workspace.element.querySelector (".cp-main").append (this.chooserView.element);
        this.replaceChildren (this.topBar.element, this.workspace.element);
        this.floatWindow = new FloatingGUIWindow (this.ctx, this);
        this.palette = new CommandPalette (this.ctx, this);

        this.disposer.add (() => { this.topBar.dispose(); this.workspace.dispose(); this.floatWindow.dispose(); this.palette.dispose();
                                   this.settingsView.dispose(); this.inspectorView.dispose(); this.chooserView.dispose(); });

        this.disposer.add (this.registerCommands());

        // session events
        const statusListener = (status) => this.updateStatus (status);
        const fileChangeListener = (message) => this.handlePatchFilesChanged (message);
        const infiniteLoopListener = () => this.handleInfiniteLoop();
        const deviceListener = (p) => this.store.set ({ deviceProperties: p });

        this.session.addStatusListener (statusListener);
        this.session.addFileChangeListener (fileChangeListener);
        this.session.addInfiniteLoopListener (infiniteLoopListener);
        this.session.addAudioDevicePropertiesListener (deviceListener);

        this.disposer.add (() =>
        {
            this.session.removeStatusListener (statusListener);
            this.session.removeFileChangeListener (fileChangeListener);
            this.session.removeInfiniteLoopListener (infiniteLoopListener);
            this.session.removeAudioDevicePropertiesListener (deviceListener);
        });

        // theme
        this.stopFollowingBrowserTheme = applyBrowserTheme (this, this.settings.global.get ("theme", "auto"));
        this.disposer.add (() => this.stopFollowingBrowserTheme?.());
        this.disposer.add (this.bridge.addThemeListener (theme => this.applyHostTheme (theme)));

        if (this.bridge.getTheme())
            this.applyHostTheme (this.bridge.getTheme());

        // (the server keeps the endpoint listeners through a rebuild, but parameters may have changed)
        this.disposer.add (this.store.subscribe (["buildNumber"], (s) => s.connection?.hub.handleNewBuild()));

        // the problem count on the drawer tab
        this.disposer.add (this.store.subscribe (["diagnostics"], (s) =>
        {
            const errors = countErrors (s.diagnostics);
            this.workspace.drawer.setBadge ("problems", s.diagnostics.length ? String (s.diagnostics.length) : "", errors ? "error" : "warning");
        }, true));

        // browser mode: choosing and dropping patches
        if (! this.isFixedPatch)
        {
            this.disposer.listen (this, "dragover", e => e.preventDefault());
            this.disposer.listen (this, "drop", e => this.handleDrop (e));
            this.disposer.add (this.store.subscribe (["connection", "loaded", "error", "statusText"], () => this.updateChooser(), true));
        }

        this.disposer.listen (document, "keydown", e => this.handleKeyDown (e));
        this.disposer.add (this.bridge.forwardUnusedKeys());

        this.session.requestSessionStatus();
        this.session.requestAudioDeviceProperties();
    }

    createGUIToolbar()
    {
        const select = el ("select", { class: "cp-select", "aria-label": "View type", title: "Which of the patch's views to show",
                                       onchange: () => this.store.set ({ guiViewType: select.value === "default" ? "" : select.value }) });
        const reloadButton = el ("button", { class: "cp-button cp-icon-button", title: "Re-create the patch's GUI (picking up changes to its files)",
                                             "aria-label": "Reload the GUI", onclick: () => this.reloadGUI() },
                                 el ("span", { class: "cp-button-glyph cp-glyph-arrow", "aria-hidden": "true" }, "⟳"));
        const toolbar = el ("div", { class: "cp-tab-tools" }, select, reloadButton);

        this.disposer.add (this.store.subscribe (["connection", "manifest"], () =>
        {
            const types = this.guiView.getAvailableViewTypes();
            toolbar.hidden = ! this.store.get ("connection");
            select.hidden = types.length < 2;
            select.replaceChildren (...types.map (t => el ("option", { value: t }, t === "custom" ? "Patch's own view" : "Generic view")));
            select.value = this.store.get ("guiViewType") || types[0] || "";
        }, true));

        return toolbar;
    }

    //==============================================================================
    createActions()
    {
        const getPatchConnection = () => this.store.get ("connection")?.patchConnection;

        return {
            toggleAudio:    () => this.session.setAudioPlaybackActive (! this.store.get ("playing")),
            reset:          () => { this.session.setAudioPlaybackActive (true); getPatchConnection()?.resetToInitialState(); },
            loadPatch:      (file) => this.loadPatch (file),
            unloadPatch:    () => this.unloadPatch(),
            showDrawer:     (tab) => this.workspace.showDrawerTab (tab),
            showConsole:    (endpointID) => { this.consoleView.showOnly (endpointID); this.workspace.showDrawerTab ("console"); },
            select:         (selection) => this.store.set ({ selection }),
            fitGraph:       () => this.graphView.fit(),
            resetGraphView: () => this.graphView.showDefaultView(),

            copyState: () => getPatchConnection()?.requestFullStoredState (state =>
            {
                this.bridge.writeClipboard (JSON.stringify (state));
                this.consoleView.add ("panel", "Copied the patch's state to the clipboard");
            }),

            pasteState: async () => this.applyStateText (await this.bridge.readClipboard()),

            /// moveGUIAside: whether opening it switches the main tab away from the GUI (not when
            /// it's re-opened as a patch loads, which starts on its own tab)
            setFloatingGUIOpen: (shouldBeOpen, moveGUIAside = true) =>
            {
                if (shouldBeOpen && ! this.store.get ("connection"))
                    return;

                if (shouldBeOpen)
                {
                    this.floatWindow.open();

                    // the point is to see the GUI on top of the results, so the GUI tab moves aside -
                    // without saving that as the patch's tab, and coming back when the window closes
                    if (moveGUIAside && this.workspace.mainTab === "gui")
                    {
                        this.workspace.showMainTab ("graph", false);
                        this.movedGUIAsideForFloat = true;
                    }
                }
                else
                {
                    this.floatWindow.close();

                    if (this.movedGUIAsideForFloat && this.workspace.mainTab === "graph")
                        this.workspace.showMainTab ("gui", false);

                    this.movedGUIAsideForFloat = false;
                }

                this.settings.patchOrGlobal.set ("floatOpen", !! shouldBeOpen);
                setPressed (this.floatButton, this.floatWindow.isOpen);
            },

            setThemePreference: (preference) =>
            {
                this.settings.global.set ("theme", preference);
                this.stopFollowingBrowserTheme?.();
                this.stopFollowingBrowserTheme = applyBrowserTheme (this, preference);
            },

            /// Whether the top bar shows the transport for every patch, not just the ones that read the timeline
            setAlwaysShowTransport: (shouldShow) =>
            {
                this.settings.global.set ("alwaysShowTransport", !! shouldShow);
                this.store.set ({ alwaysShowTransport: !! shouldShow });
            },

            cycleTheme: () => this.ctx.actions.setThemePreference (this.dataset.theme === "light" ? "dark" : "light"),

            showCommandPalette: (initialText) => this.palette.open (initialText),

            /// Shows an endpoint's widget in the I/O tab and focuses its control, or, for one
            /// that isn't there (a hidden parameter), its row in the inspector
            revealEndpoint: (endpointID) =>
            {
                this.store.set ({ selection: { kind: "endpoint", endpointID } });

                if (this.ioView.hasWidgetFor (endpointID))
                {
                    this.workspace.showMainTab ("io");
                    this.ioView.reveal (endpointID);
                }
                else
                {
                    this.workspace.setInspectorOpen (true);
                    this.inspectorView.revealParameter (endpointID);
                }
            },

            /// Opens an audio endpoint's spectrum analyser in the I/O tab, and focuses it
            showSpectrum: (endpointID) =>
            {
                this.workspace.showMainTab ("io");

                if (! this.ioView.setSpectrumShown (endpointID, true))
                    return false;

                const view = this.ioView.findSpectrum (endpointID);
                view?.element.scrollIntoView ({ block: "nearest" });
                view?.canvas.focus ({ preventScroll: true });
                return true;
            },
        };
    }

    //==============================================================================
    /// The panel's own commands (with the keyboard shortcuts), and the providers of the ones
    /// that come from the patch: its endpoints, parameters, views, and the other patches
    registerCommands()
    {
        const commands = this.ctx.commands;
        const actions = this.ctx.actions;
        const s = this.store;
        const hasPatch = () => !! s.get ("connection");
        const disposer = new Disposer();

        // (remembered across sessions, so the palette offers the usual ones first)
        const run = commands.run.bind (commands);
        commands.run = (c) => { const ran = run (c); if (ran) this.settings.global.set ("recentCommands", commands.recent); return ran; };
        disposer.add (() => { commands.run = run; });

        disposer.add (commands.registerAll ([
            { id: "palette.show",      title: "Show all commands",            category: "Panel", shortcut: "Mod+K",
              run: () => actions.showCommandPalette() },
            { id: "audio.toggle",      title: "Start or stop audio",          category: "Audio", shortcut: "Space", keywords: ["play", "mute"],
              run: () => actions.toggleAudio() },
            { id: "patch.reset",       title: "Reset the patch",              category: "Patch", shortcut: "R", keywords: ["initial state"],
              run: () => actions.reset() },
            { id: "view.gui",          title: "Show the GUI",                 category: "View",  shortcut: "1", kind: "view",
              run: () => this.workspace.showMainTab ("gui") },
            { id: "view.graph",        title: "Show the graph",               category: "View",  shortcut: "2", kind: "view",
              run: () => this.workspace.showMainTab ("graph") },
            { id: "view.io",           title: "Show the inputs and outputs",  category: "View",  shortcut: "3", kind: "view", keywords: ["I/O", "endpoints"],
              run: () => this.workspace.showMainTab ("io") },
            { id: "graph.fit",         title: "Fit the graph to the view",    category: "Graph", shortcut: "F", keywords: ["zoom"],
              run: () => { this.workspace.showMainTab ("graph"); actions.fitGraph(); } },
            { id: "graph.resetView",   title: "Reset the graph view",         category: "Graph", shortcut: "0", keywords: ["zoom"],
              run: () => { this.workspace.showMainTab ("graph"); actions.resetGraphView(); } },
            { id: "gui.float",         title: "Float the GUI in a window",    category: "View",  shortcut: "G", keywords: ["dock", "window"],
              run: () => actions.setFloatingGUIOpen (! this.floatWindow.isOpen), isEnabled: hasPatch },
            { id: "console.filter",    title: "Filter the console",           category: "Console", shortcut: "/", keywords: ["search", "log"],
              run: () => { this.workspace.showDrawerTab ("console"); this.consoleView.focusFilter(); } },
            { id: "view.console",      title: "Show the console",             category: "View", kind: "view", keywords: ["log", "drawer"],
              run: () => this.workspace.showDrawerTab ("console") },
            { id: "view.problems",     title: "Show the problems",            category: "View", kind: "view", keywords: ["errors", "warnings", "drawer"],
              run: () => this.workspace.showDrawerTab ("problems") },
            { id: "view.code",         title: "Show the generated code",      category: "View", kind: "view", keywords: ["codegen", "drawer"],
              run: () => this.workspace.showDrawerTab ("code") },
            { id: "view.inspector",    title: "Show or hide the inspector",   category: "View", kind: "view", keywords: ["parameters", "sidebar"],
              run: () => this.workspace.setInspectorOpen (! this.workspace.isInspectorOpen) },
            { id: "view.drawer",       title: "Show or hide the drawer",      category: "View", kind: "view",
              run: () => this.workspace.setDrawerOpen (! this.workspace.isDrawerOpen) },
            { id: "gui.reload",        title: "Reload the patch's GUI",       category: "Patch", keywords: ["refresh"],
              run: () => this.reloadGUI(), isEnabled: hasPatch },
            { id: "state.copy",        title: "Copy the patch's state",       category: "Patch", keywords: ["clipboard"],
              run: () => actions.copyState(), isEnabled: hasPatch },
            { id: "state.paste",       title: "Paste a state into the patch", category: "Patch", keywords: ["clipboard"],
              run: () => actions.pasteState(), isEnabled: hasPatch },
            { id: "patch.unload",      title: "Unload the patch",             category: "Patch", keywords: ["close"],
              run: () => actions.unloadPatch(), isEnabled: () => ! this.isFixedPatch && (s.get ("loaded") || !! s.get ("error")) },
            { id: "settings.show",     title: "Open the audio settings",      category: "Audio", keywords: ["device", "sample rate", "block size"],
              run: () => this.topBar.showSettings() },
            { id: "cpu.history",       title: "Show CPU history",             category: "Audio", keywords: ["load", "performance", "dropouts", "overruns", "budget"],
              run: () => this.topBar.showCPUHistory() },
            { id: "panel.about",       title: "About Cmajor",                 category: "Panel", keywords: ["version", "website", "licence", "help"],
              run: () => this.topBar.showAbout() },
            { id: "theme.light",       title: "Use the light theme",          category: "Theme", run: () => actions.setThemePreference ("light"), isEnabled: () => ! s.get ("hostTheme") },
            { id: "theme.dark",        title: "Use the dark theme",           category: "Theme", run: () => actions.setThemePreference ("dark"),  isEnabled: () => ! s.get ("hostTheme") },
            { id: "theme.auto",        title: "Follow the system theme",      category: "Theme", run: () => actions.setThemePreference ("auto"),  isEnabled: () => ! s.get ("hostTheme") },
        ]));

        // MIDI: panic, and the first MIDI input's octave (see widgets/midi-controls.js)
        disposer.add (commands.registerAll (createMIDICommands ({
            getMIDIInputs: () => (s.get ("inputs") ?? []).filter (e => e.purpose === "midi in"),
            getPatchConnection: () => s.get ("connection")?.patchConnection,
            settings: this.settings })));

        // the first audio input: a test signal, or the live input (see widgets/audio-input-source.js)
        disposer.add (commands.registerAll (createAudioInputCommands ({
            getAudioInputs: () => (s.get ("inputs") ?? []).filter (e => e.purpose === "audio in"),
            getSession: () => hasPatch() ? this.ctx.session : null,
            settings: this.settings })));

        // "Show spectrum for <endpoint>", for each audio input and output (see widgets/spectrum-analyser.js)
        disposer.add (commands.addProvider (createSpectrumCommandProvider ({
            getAudioEndpoints: () => hasPatch() ? [...(s.get ("inputs") ?? []), ...(s.get ("outputs") ?? [])] : [],
            showSpectrum: (endpointID) => actions.showSpectrum (endpointID) })));

        // the session's transport, while the top bar shows it (see shell/transport-control.js)
        disposer.add (commands.registerAll (createTransportCommands (() => this.topBar.transport)));

        // recording: start/stop (C), a marker (M), show the recordings (see views/recordings-view.js)
        disposer.add (commands.registerAll (createRecordingCommands ({ controller: this.recordingController,
                                                                       showRecordings: () => this.workspace.showDrawerTab ("recordings") })));

        // parameter fuzzing: randomise, restore, stop the automation (see core/parameter-fuzzer.js)
        disposer.add (commands.registerAll (createFuzzCommands (() => s.get ("connection")?.fuzzer ?? null)));

        // the patch's endpoints and parameters
        disposer.add (commands.addProvider (() =>
        {
            if (! hasPatch())
                return [];

            const toCommand = (endpoint, isInput) =>
            {
                const isParameter = endpoint.purpose === "parameter";

                return { id: `endpoint:${endpoint.endpointID}`, kind: isParameter ? "parameter" : "endpoint",
                         title: getEndpointDisplayName (endpoint), detail: endpoint.endpointID,
                         category: isParameter ? "Parameter" : (isInput ? "Input" : "Output"), keywords: [endpoint.purpose ?? ""],
                         run: () => actions.revealEndpoint (endpoint.endpointID) };
            };

            return [...(s.get ("inputs") ?? []).map (e => toCommand (e, true)),
                    ...(s.get ("outputs") ?? []).map (e => toCommand (e, false))];
        }));

        // the server's patches (in a browser; in VS Code, the editor chooses the patch)
        disposer.add (commands.addProvider (() =>
        {
            if (this.isFixedPatch)
                return [];

            return (s.get ("availablePatches") ?? []).filter (p => p?.manifestFile && p.manifestFile !== s.get ("manifestFile"))
                     .map (p => ({ id: `patch:${p.manifestFile}`, kind: "patch", title: `Load ${p.name || p.manifestFile.split (/[\\/]/).pop()}`,
                                   detail: p.manifestFile, category: "Patch", run: () => actions.loadPatch (p.manifestFile) }));
        }));

        return () => disposer.dispose();
    }

    applyStateText (text)
    {
        const patchConnection = this.store.get ("connection")?.patchConnection;

        if (! text || ! patchConnection)
            return;

        try
        {
            const state = JSON.parse (text);

            if (state && typeof state === "object")
            {
                patchConnection.sendFullStoredState (state);
                this.consoleView.add ("panel", "Applied a state from the clipboard");
                return;
            }
        }
        catch (e) {}

        this.consoleView.add ("panel", "The clipboard doesn't contain a patch state", "warning");
    }

    applyHostTheme (theme)
    {
        applyHostTheme (this, theme);
        this.store.set ({ hostTheme: !! theme });
    }

    //==============================================================================
    loadPatch (patchURL)
    {
        if (patchURL && typeof patchURL === "string")
        {
            if (patchURL !== this.store.get ("manifestFile"))
                this.disposeConnection();

            this.session.loadPatch (patchURL);
        }
        else
        {
            this.unloadPatch();
        }
    }

    unloadPatch()
    {
        this.disposeConnection();
        this.store.set ({ manifestFile: "", manifest: null });
        this.session.loadPatch (null);
    }

    handleMessageFromVSCode (message)
    {
        if (this.bridge.handleMessage (message))
            return;

        if (message?.patchToLoad)
            this.loadPatch (message.patchToLoad);
        else if (message?.clipboardText !== undefined)
            this.applyStateText (message.clipboardText);
        else if (message?.unload)
            this.unloadPatch();
    }

    //==============================================================================
    updateStatus (status)
    {
        // (one batch, so that e.g. a new connection and its endpoints arrive together, and the
        // views that show them are built once)
        this.store.batch (() => this.applyStatus (status));
        this.bridge.notifyStatus (status);
    }

    applyStatus (status)
    {
        const hadConnection = !! this.store.get ("connection");
        const previousError = this.store.get ("error");
        const changed = new Set (applySessionStatus (this.store, status));

        if (changed.has ("error"))
        {
            this.store.set ({ diagnostics: parseDiagnostics (status.error) });

            if (status.error)
            {
                this.consoleView.add ("panel", "Build failed: " + (parseDiagnostics (status.error)[0]?.message ?? "see Problems"), "error");

                if (! previousError)
                    this.workspace.showDrawerTab ("problems");
            }
        }

        if (status.connected && ! this.hasRequestedPatchList)
        {
            this.hasRequestedPatchList = true;
            this.requestPatchList();
        }
        else if (! status.connected)
        {
            this.hasRequestedPatchList = false;
        }

        this.updateConnection (status);

        // a custom view may depend on the manifest or the endpoints, so re-create it if they change
        if (hadConnection && this.store.get ("connection") && status.loaded
             && (changed.has ("manifest") || changed.has ("inputs") || changed.has ("outputs")))
            this.store.set ({ guiGeneration: this.store.get ("guiGeneration") + 1 });

        if (changed.has ("lastBuildTime") && status.loaded)
            this.consoleView.add ("panel", (status.status || "Loaded") + (status.log ? ` (${status.log.split ("\n")[0]})` : ""));
    }

    async requestPatchList()
    {
        const list = await this.session.requestAvailablePatchList();
        this.store.set ({ availablePatches: Array.isArray (list) ? list : [] });

        if (this.isFixedPatch && list?.length > 0 && ! this.store.get ("loaded"))
            this.loadPatch (list[0]?.manifestFile);
    }

    /// Creates a PatchConnection when a patch has loaded. It's kept while the same patch
    /// rebuilds (or fails to), or the server is briefly out of touch, so that its views and
    /// widgets survive a source change or a long build.
    updateConnection (status)
    {
        const current = this.store.get ("connection");
        const manifestFile = this.store.get ("manifestFile");

        if (status.loaded)
        {
            if (current && current.manifestFile === manifestFile)
                return;

            this.disposeConnection();
            this.createConnection (manifestFile);
        }
        else if (current && ! isTransientStatus (status))
        {
            this.disposeConnection();
        }
    }

    createConnection (manifestFile)
    {
        const patchConnection = this.session.createPatchConnection ({
            // after the patch's files change, its views load fresh copies of them (see addVersionFolder())
            transformResourceAddress: (address) => addVersionFolder (address, this.session.status?.httpRootURL, this.resourceVersion),

            // The views are kept alive through a build error, showing the last good build, but other
            // hosts replace the view when a build fails, so views don't expect the status that comes
            // with an error (it has no endpoints, and may have no manifest). The panel gets errors from
            // the session status instead, so it's only the views and widgets that don't see these.
            filterMessage: (msg) => ! (msg?.type === "status" && msg.message?.error),
        });

        const hub = new EndpointDataHub (patchConnection);
        const patchSettings = this.settings.setPatch (manifestFile);

        // parameter fuzzing: one per patch, following its parameters through rebuilds (see core/parameter-fuzzer.js)
        const fuzzer = new ParameterFuzzer ({ patchConnection, hub, scheduler: frameScheduler, settings: this.settings.patch,
                                              getSampleRate: () => this.store.get ("deviceProperties")?.rate });
        fuzzer.disposer.add (this.store.subscribe (["inputs"], (s) => fuzzer.setParameters (getParameters (s)), true));

        this.store.set ({ connection: { patchConnection, hub, manifestFile, fuzzer }, patchSettings,
                          guiGeneration: this.store.get ("guiGeneration") + 1, selection: null });
        this.workspace.applyPrefs();

        if (this.settings.patchOrGlobal.get ("floatOpen", false))
            this.ctx.actions.setFloatingGUIOpen (true, false);
    }

    disposeConnection()
    {
        const current = this.store.get ("connection");

        if (current)
        {
            this.floatWindow?.close();
            this.movedGUIAsideForFloat = false;
            // (the fuzzer ends its gestures while the connection can still send them)
            current.fuzzer?.dispose();
            this.store.set ({ connection: null, patchSettings: this.settings.setPatch (null), selection: null });
            current.hub.dispose();
            current.patchConnection.dispose();
        }
    }

    /// Re-creates the patch's GUI views, with fresh copies of their files
    reloadGUI()
    {
        ++this.resourceVersion;
        this.store.set ({ guiGeneration: this.store.get ("guiGeneration") + 1 });
    }

    handlePatchFilesChanged (message)
    {
        if (message?.assetFilesChanged || message?.manifestChanged)
        {
            this.consoleView.add ("panel", message.manifestChanged ? "The manifest changed: reloading the GUI" : "Patch files changed: reloading the GUI");
            this.reloadGUI();
        }
    }

    handleInfiniteLoop()
    {
        this.bridge.notifyInfiniteLoop();
        this.consoleView.add ("panel", "The patch got stuck in an infinite loop", "error");
    }

    updateChooser()
    {
        const s = this.store.state;
        this.chooserView.element.hidden = !! (s.connection || s.loaded || s.error || s.statusText?.startsWith ("Loading"));
    }

    async handleDrop (e)
    {
        e.preventDefault();
        const manifest = await registerDroppedPatchFiles (this.session, e.dataTransfer);

        if (manifest)
            this.loadPatch (manifest);
    }

    //==============================================================================
    handleKeyDown (e)
    {
        if (e.defaultPrevented || ! this.isConnected)
            return;

        const path = e.composedPath();
        const target = path[0];

        // Shortcuts with Cmd/Ctrl (the palette) work in the panel's own fields too, but not in
        // the patch's GUIs, which own their keys
        if (e.ctrlKey || e.metaKey)
        {
            if (! path.some (n => n.matches?.(".cp-gui-holder, .cp-float-body")))
            {
                const command = this.ctx.commands.findShortcut (e);

                if (command)
                {
                    e.preventDefault();
                    this.ctx.commands.run (command);
                }
            }

            return;
        }

        if (e.altKey)
            return;

        // Leave keys alone in text fields, buttons, the patch's GUIs and the on-screen keyboards.
        // (The whole composed path is checked, as closest() stops at a shadow root, so it would
        // miss e.g. an input inside a patch view's shadow DOM.)
        if (path.some (n => n.matches?.(keyboardOwnerSelector)) || target?.isContentEditable)
        {
            if (e.key === "Escape" && target?.matches?.("input"))
                target.blur();

            return;
        }

        // (holding a key down would toggle the audio or flip between tabs many times a second)
        if (e.repeat)
            return;

        // (the shortcuts are defined with their commands: see registerCommands())
        const command = this.ctx.commands.findShortcut (e);

        if (command)
        {
            e.preventDefault();
            this.ctx.commands.run (command);
        }
    }
}

//==============================================================================
/// The elements whose keys aren't panel shortcuts
const keyboardOwnerSelector = "input, textarea, select, button, [contenteditable], .cp-gui-holder, .cp-float-body, .cp-keyboard, [role=tab], [role=separator]";

function addStyleSheets()
{
    const href = new URL ("./theme/panel.css", import.meta.url).href;

    if (! document.querySelector (`link[href="${href}"]`))
        document.head.append (el ("link", { rel: "stylesheet", href }));
}

/** Once the patch's files have changed (version > 0), puts an address of one of them in a
    version folder: "<root>/ui/view.js" becomes "<root>/~v3/ui/view.js". The server ignores
    the folder (removeVersionFolder() in cmaj_PatchPlayerServer.cpp), but to the browser it's a
    new URL, so it loads a fresh copy, and so do the modules that it imports by relative paths.
    (A query string would only do that for the file itself.)

    The limit: a custom element class can't be redefined, so a view that guards its own
    customElements.define() keeps its old class until the page is reloaded. */
export function addVersionFolder (address, rootURL, version)
{
    if (! version || typeof address !== "string" || ! rootURL)
        return address;

    const root = rootURL.endsWith ("/") ? rootURL : rootURL + "/";
    return address.startsWith (root) ? root + `~v${version}/` + address.substring (root.length) : address;
}

if (! customElements.get ("cmaj-patch-panel"))
    customElements.define ("cmaj-patch-panel", PatchPanel);
