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
import PianoKeyboard from "../cmaj_api/cmaj-piano-keyboard.js"
import LevelMeter from "./helpers/cmaj-level-meter.js"
import WaveformDisplay from "./helpers/cmaj-waveform-display.js";
import * as patchViewUtils from "../cmaj_api/cmaj-patch-view.js"
import * as midi from "../cmaj_api/cmaj-midi-helpers.js"
import "./cmaj-cpu-meter.js"
import "./cmaj-graph.js"
import { getCmajorVersion } from "../cmaj_api/cmaj-version.js"

const maxUploadFileSize = 1024 * 1024 * 50;

function showErrorAlert (message)
{
    if (window.sendMessageToVSCode)
        window.sendMessageToVSCode ({ showErrorAlert: message });
    else
        alert (message);
}

window.openSourceFile = (file) =>
{
    if (window.sendMessageToVSCode)
        window.sendMessageToVSCode ({ showSourceFile: file });
    else
        // probably blocked by the browser, but this would at least open the file..
        window.open ("file://" + file.split(":")[0], "_blank");
}

function createFileLink (s)
{
    s = JSON.stringify (s);
    s = s.substring (1, s.length - 1);
    return `javascript:openSourceFile('${s}');`;
}

window.openURLInNewWindow = (url) =>
{
    if (window.sendMessageToVSCode)
        window.sendMessageToVSCode ({ openURLInNewWindow: url });
    else
        window.open (url, "_blank");
}

function openTextDocument (content, language)
{
    window.sendMessageToVSCode?.({ openTextDocument: content,
                                   language: language });
}

function handleInfiniteLoopAlert()
{
    window.sendMessageToVSCode?.({ serverFailedWithInfiniteLoop: true });
}

function getMessageListAsString (messages)
{
    if (Array.isArray (messages))
    {
        let result = "";

        for (let i = 0; i < messages.length; ++i)
        {
            if (i != 0)
                result += "\n";

            result += getmessagesDescription (messages[i]);
        }

        return result;
    }

    let desc = messages.fullDescription ? messages.fullDescription
                                        : messages.message;

    if (messages.annotatedLine)
        desc += "\n" + messages.annotatedLine;

    return desc;
}

//==============================================================================
class EndpointControlBase  extends HTMLElement
{
    constructor (patchConnection, endpointInfo)
    {
        super();

        this.patchConnection = patchConnection;
        this.endpointInfo = endpointInfo;
    }

    initialise (controls, bottomControl)
    {
        let name = this.endpointInfo.endpointID;
        const annotation = this.endpointInfo.annotation;

        if (annotation)
            if (typeof annotation.name == "string" && annotation.name != "")
                name = annotation.name;

        if (this.endpointInfo.source)
            name = `<a href="${createFileLink (this.endpointInfo.source.toString())}">${name}</a>`;

        this.innerHTML =
         `<div class="cmaj-io-control">
            <div class="cmaj-io-control-content">
              ${controls}
            </div>
            <div class="cmaj-io-label-holder">
            ${bottomControl ? bottomControl : ""}
            <div class="cmaj-io-label"><p>${name}</p></div>
            </div>
          </div>`;
    }

    static getCSS()
    {
        return `
        .cmaj-io-control {
            display: flex;
            padding: 0.2rem;
            margin: 0.6rem;
            background: #22222288;
            box-shadow: 0 0.3rem 0.4rem 0 #00000030;
            flex-flow: column nowrap;
            justify-content: flex-start;
            align-items: center;
        }

        .cmaj-io-control-content {
            flex-basis: 6.5rem;
        }

        .cmaj-io-label-holder {
            padding: 0 0.8rem;
        }

        .cmaj-io-label {
            display: inline-block;
            padding: 0.2rem 1.2rem;
        }

        .cmaj-io-label p {
            margin: 0;
            font-size: 0.8rem;
            color: #eee;
        }

        cmaj-level-meter {
            display: inline-block;
            width: 3.5rem;
            height: 6.5rem;
            min-width: 3.5rem;
            margin: 0.1rem;
            flex-basis: 6.5rem;
            flex-grow: 0;
        }

        cmaj-waveform-display {
            display: inline-block;
            width: 12rem;
            height: 6.5rem;
            min-width: 12rem;
            margin: 0.1rem;
            flex-basis: 24rem;
            flex-grow: 0;
        }`;
    }
}

//==============================================================================
class ConsoleEventControl  extends EndpointControlBase
{
    constructor (patchConnection, endpointInfo)
    {
        super (patchConnection, endpointInfo);

        this.initialise (`<textarea class="cmaj-console" rows="20" cols="100" readonly=true></textarea>`,
                         `<button class="cmaj-clear-button">Clear</button>`);
        this.textArea = this.querySelector ("textarea");
        this.newLineForEachItem = endpointInfo.endpointID != "console";

        const clearButton = this.querySelector (".cmaj-clear-button");
        clearButton.onclick = () => { this.textArea.value = ""; this.textArea.scrollTop = this.textArea.scrollHeight; }
    }

    connectedCallback()
    {
        this.callback = event => this.write (event);
        this.patchConnection.addEndpointListener (this.endpointInfo.endpointID, this.callback);
    }

    disconnectedCallback()
    {
        this.patchConnection.removeEndpointListener (this.endpointInfo.endpointID, this.callback);
    }

    write()
    {
        let message = "";

        for (let arg of arguments)
        {
            let m = this.formatMessage (arg);

            if (this.newLineForEachItem && ! m.endsWith ("\n"))
                m += "\n";

            message += m;
        }

        let text = "";

        // look for an ESC char, and clear the console if found
        const lastEscChar = message.lastIndexOf ("\u001b");

        if (lastEscChar >= 0)
        {
            message = message.substring (lastEscChar + 1);
        }
        else
        {
            text = this.textArea.value;
            const maxConsoleSize = 5000;

            while (text.length > maxConsoleSize)
            {
                let breakPoint = text.indexOf ("\n", text.length - maxConsoleSize) + 1;

                if (breakPoint <= 0)
                    breakPoint = text.length - maxConsoleSize;

                text = text.substring (breakPoint);
            }
        }

        this.textArea.value = text + message;
        this.textArea.scrollTop = this.textArea.scrollHeight;
    }

    formatMessage (m)
    {
        if (typeof m == "string")
            return m;

        if (m._type)
        {
            if (m._type == "Message" && m.message)
                return midi.getMIDIDescription (m.message);

            const type = m._type;
            delete m._type;
            return type + " " + JSON.stringify (m) + "\n";
        }

        return JSON.stringify (m);
    }

    static getCSS()
    {
        return `
        .cmaj-console {
            display: inline-block;
            width: 100%;
            height: 8rem;
            min-height: 5rem;
            resize: vertical;
            background: #222;
            color: #5e5;
        }`;
    }
}

//==============================================================================
class MIDIInputControl  extends EndpointControlBase
{
    constructor (patchConnection, endpointInfo)
    {
        super (patchConnection, endpointInfo);

        this.initialise ("<cmaj-panel-piano-keyboard></cmaj-panel-piano-keyboard>");
        this.keyboard = this.querySelector ("cmaj-panel-piano-keyboard");

        this.keyboard.setAttribute ("root-note", 24);
        this.keyboard.setAttribute ("note-count", 63);
    }

    connectedCallback()
    {
        this.keyboard.attachToPatchConnection (this.patchConnection, this.endpointInfo.endpointID);
    }

    disconnectedCallback()
    {
        this.keyboard.detachPatchConnection (this.patchConnection);
    }
}

//==============================================================================
class AudioLevelControl  extends EndpointControlBase
{
    constructor (patchConnection, endpointInfo)
    {
        super (patchConnection, endpointInfo);

        this.initialise (`<cmaj-level-meter></cmaj-level-meter>
                          <cmaj-waveform-display></cmaj-waveform-display>`);

        this.meter = this.querySelector ("cmaj-level-meter");
        this.waveform = this.querySelector ("cmaj-waveform-display");

        this.meter.setNumChans (endpointInfo.numAudioChannels);
        this.waveform.setNumChans (endpointInfo.numAudioChannels);
    }

    connectedCallback()
    {
        this.callback = event =>
        {
            for (let chan = 0; chan < event.min.length; ++chan)
            {
                this.meter.setChannelMinMax (chan, event.min[chan], event.max[chan]);
                this.waveform.setChannelMinMax (chan, event.min[chan], event.max[chan]);
            }
        };

        this.patchConnection.addEndpointListener (this.endpointInfo.endpointID, this.callback, 400);
    }

    disconnectedCallback()
    {
        this.patchConnection.removeEndpointListener (this.endpointInfo.endpointID, this.callback);
    }
}

//==============================================================================
class AudioInputControl  extends EndpointControlBase
{
    constructor (patchConnection, endpointInfo)
    {
        super (patchConnection, endpointInfo);
        this.session = patchConnection.session;

        this.initialise (`<cmaj-level-meter></cmaj-level-meter>
                          <cmaj-waveform-display></cmaj-waveform-display>`,
                          `<button class="cmaj-live-button">Live</button>
                           <button class="cmaj-file-button">File</button>
                           <button class="cmaj-mute-button">Mute</button>
                           <input type="file" hidden>`);

        this.meter = this.querySelector ("cmaj-level-meter");
        this.waveform = this.querySelector ("cmaj-waveform-display");
        this.fileButton = this.querySelector (".cmaj-file-button");
        this.liveButton = this.querySelector (".cmaj-live-button");
        this.muteButton = this.querySelector (".cmaj-mute-button");
        this.fileSelector = this.querySelector ("input");

        this.ondragover = e => this.handleDragOver (e);
        this.ondrop = e => this.handleDrop (e);

        this.meter.setNumChans (endpointInfo.numAudioChannels);
        this.waveform.setNumChans (endpointInfo.numAudioChannels);

        this.modeButtons = [
            { button: this.fileButton, mode: "file" },
            { button: this.liveButton, mode: "live" },
            { button: this.muteButton, mode: "mute" }
        ];

        this.fileButton.onclick = () => this.fileSelector.click();
        this.liveButton.onclick = () => this.setAudioInputSource (false);
        this.muteButton.onclick = () => this.setAudioInputSource (true);

        this.fileSelector.onchange = () => this.fileChosen();

        this.audioCallback = event => {
            for (let chan = 0; chan < event.min.length; ++chan)
            {
                this.meter.setChannelMinMax (chan, event.min[chan], event.max[chan]);
                this.waveform.setChannelMinMax (chan, event.min[chan], event.max[chan]);
            }
        };

        this.modeCallback = mode => this.updateMode (mode);
    }

    connectedCallback()
    {
        this.patchConnection.addEndpointListener (this.endpointInfo.endpointID, this.audioCallback, 400);
        this.session.addAudioInputModeListener (this.endpointInfo.endpointID, this.modeCallback);
        this.session.requestAudioInputMode (this.endpointInfo.endpointID);
    }

    disconnectedCallback()
    {
        this.patchConnection.removeEndpointListener (this.endpointInfo.endpointID, this.audioCallback);
        this.session.removeAudioInputModeListener (this.endpointInfo.endpointID, this.modeCallback);
    }

    updateMode (mode)
    {
        for (const b of this.modeButtons)
        {
            if (mode == b.mode)
                b.button.classList.add ("cmaj-selected-button");
            else
                b.button.classList.remove ("cmaj-selected-button");
        }
    }

    setAudioInputSource (shouldMute, fileToPlay)
    {
        this.session.setAudioInputSource (this.endpointInfo.endpointID, shouldMute, fileToPlay);
    }

    setSourceFile (file)
    {
        if (file)
        {
            const reader = new FileReader();

            reader.onloadend = (e) =>
            {
                if (e.total > maxUploadFileSize)
                    showErrorAlert ("File too big to upload!");
                else
                    this.setAudioInputSource (false, e.target.result);
            }

            reader.readAsArrayBuffer (file);
        }
    }

    fileChosen()
    {
        this.setSourceFile (this.fileSelector.files[0]);
        this.fileSelector.value = "";
    }

    handleDragOver (e)
    {
        e.preventDefault();
    }

    handleDrop (e)
    {
        e.preventDefault();

        for (const file of e.dataTransfer?.files)
        {
            console.log (file);
            this.setSourceFile (file);
            break;
        }
    }
}

//==============================================================================
class AudioDevicePropertiesPanel  extends HTMLElement
{
    constructor (session)
    {
        super();
        this.session = session;
        this.listener = p => this.handleAudioDevicePropertiesChanged (p);
    }

    connectedCallback()
    {
        this.session.addAudioDevicePropertiesListener (this.listener);
        this.session.requestAudioDeviceProperties();
    }

    disconnectedCallback()
    {
        this.session.removeAudioDevicePropertiesListener (this.listener);
    }

    handleAudioDevicePropertiesChanged (p)
    {
        this.currentProperties = p;
        let html = "";

        function addItem (label, id, list, current)
        {
            let options = "";

            for (let item of list)
                options += `<option value="${item}" ${item == current ? "selected" : ""}>${item}</option>`;

            html += `<div class="cmaj-device-io-item">
                      <label for="${id}">${label}:</label>
                      <select id="${id}" name="${label}">${options}</select>
                     </div>`;
        }

        if (p.availableAPIs)
            addItem ("Audio API", "cmaj-device-io-api", p.availableAPIs, p.audioAPI);

        if (p.availableOutputDevices)
            addItem ("Output Device", "cmaj-device-io-out", p.availableOutputDevices, p.output);

        if (p.availableInputDevices)
            addItem ("Input Device", "cmaj-device-io-in", p.availableInputDevices, p.input);

        if (p.sampleRates)
            addItem ("Sample Rate", "cmaj-device-io-rate", p.sampleRates, p.rate);

        if (p.blockSizes)
            addItem ("Block Size", "cmaj-device-io-blocksize", p.blockSizes, p.blockSize);

        this.innerHTML = html;

        this.querySelector ("#cmaj-device-io-api").onchange       = e => { this.setAudioAPI (e.target.value); };
        this.querySelector ("#cmaj-device-io-out").onchange       = e => { this.setOutputDevice (e.target.value); };
        this.querySelector ("#cmaj-device-io-in").onchange        = e => { this.setInputDevice (e.target.value); };
        this.querySelector ("#cmaj-device-io-rate").onchange      = e => { this.setRate (e.target.value); };
        this.querySelector ("#cmaj-device-io-blocksize").onchange = e => { this.setBlockSize (e.target.value); };
    }

    setAudioAPI (newAPI)
    {
        this.currentProperties.audioAPI = newAPI;
        this.session.setAudioDeviceProperties (this.currentProperties);
    }

    setOutputDevice (newDevice)
    {
        this.currentProperties.output = newDevice;
        this.session.setAudioDeviceProperties (this.currentProperties);
    }

    setInputDevice (newDevice)
    {
        this.currentProperties.input = newDevice;
        this.session.setAudioDeviceProperties (this.currentProperties);
    }

    setRate (newRate)
    {
        this.currentProperties.rate = newRate;
        this.session.setAudioDeviceProperties (this.currentProperties);
    }

    setBlockSize (newSize)
    {
        this.currentProperties.blockSize = newSize;
        this.session.setAudioDeviceProperties (this.currentProperties);
    }

    static getCSS()
    {
        return `
        .cmaj-audio-device-panel {
            color: var(--foreground);
            display: block;
            margin-top: 0.8rem;
            margin-bottom: 0.8rem;
        }

        .cmaj-audio-device-panel label {
            display: inline-block;
            width: 8rem;
            text-align: right;
        }

        .cmaj-device-io-item {
            display: block;
            margin-bottom: 0.4rem;
        }
    `;
    }
}

//==============================================================================
class CodeGenPanel  extends HTMLElement
{
    constructor (session)
    {
        super();

        this.codeGenTabs = [];

        this.innerHTML = `<div class="cmaj-codegen-tabs"></div>
                          <div class="cmaj-codegen-listing"></div>
                          <button class="cmaj-open-codegen-button">Open with editor</button>`;

        this.codeGenTabsHolder   = this.querySelector (".cmaj-codegen-tabs");
        this.codeGenListing      = this.querySelector (".cmaj-codegen-listing");
        this.showCodeButton      = this.querySelector (".cmaj-open-codegen-button");

        this.showCodeButton.onclick = () => this.openCodeEditor();
    }

    setSession (session)
    {
        this.session = session;
    }

    getDisplayNameForCodeGenTarget (target)
    {
        switch (target)
        {
            case "cpp":             return "C++";
            case "javascript":      return "Javascript/WebAssembly";
            case "wast":            return "WAST";
            case "llvm":            return "LLVM native";
            default:                return target;
        }
    }

    getVScodeLanguageName (target)
    {
        switch (target)
        {
            case "cpp":           return "cpp";
            case "wast":          return "wat";
            case "javascript":    return "javascript";
            default: break;
        }
    }

    refreshCodeGenTabs (status)
    {
        let targetList = status.codeGenTargets;
        this.style.display = targetList?.length > 0 ? "flex" : "none";

        while (this.codeGenTabsHolder.firstChild)
            this.codeGenTabsHolder.removeChild (this.codeGenTabsHolder.lastChild);

        while (this.codeGenListing.firstChild)
            this.codeGenListing.removeChild (this.codeGenListing.lastChild);

        this.codeGenTabs = [];

        if (targetList?.length > 0)
        {
            for (const name of targetList)
            {
                const tab = document.createElement ("div");
                tab.classList.add ("cmaj-codegen-tab");
                tab.innerHTML = `<p>${this.getDisplayNameForCodeGenTarget (name)}</p>`;
                tab.onclick = () => this.selectCodeGenTab (name);
                this.codeGenTabsHolder.appendChild (tab);

                const listing = document.createElement ("textarea");
                listing.rows = 20;
                listing.cols = 100;
                listing.readOnly = true;
                listing.wrap = "off";
                this.codeGenListing.appendChild (listing);

                this.codeGenTabs.push ({ name: name,
                                        tab: tab,
                                        listing: listing });
            }
        }

        this.updateActiveCodeGenTab();
    }

    updateActiveCodeGenTab()
    {
        this.activeTab = null;

        for (const tab of this.codeGenTabs)
        {
            if (this.activeCodeGenName == tab.name)
            {
                this.activeTab = tab;
                tab.tab.classList.add ("cmaj-active-tab");
            }
            else
            {
                tab.tab.classList.remove ("cmaj-active-tab");
            }

            tab.listing.style.display = (this.activeCodeGenName == tab.name) ? "block" : "none";
        }

        this.refreshButtonState();
    }

    selectCodeGenTab (codeGenType)
    {
        this.activeCodeGenName = codeGenType;
        this.updateActiveCodeGenTab();

        for (const tab of this.codeGenTabs)
        {
            if (this.activeCodeGenName == tab.name)
            {
                this.postCodeGenRequest (tab);
                break;
            }
        }
    }

    postCodeGenRequest (tab)
    {
        if (! tab.isCodeGenPending)
        {
            tab.isCodeGenPending = true;

            this.session.requestGeneratedCode (tab.name, {},
                message => {
                    tab.isCodeGenPending = false;

                    if (message.code)
                        tab.listing.value = message.code;
                    else if (message.messages)
                        tab.listing.value = getMessageListAsString (message.messages);

                    this.refreshButtonState();
                });
        }
    }

    refreshButtonState()
    {
        const canShow = window.sendMessageToVSCode && this.activeTab?.listing?.value?.length > 0;

        this.showCodeButton.style.display = canShow ? "block" : "none";
    }

    openCodeEditor()
    {
        const text = this.activeTab?.listing?.value;

        if (text?.length > 0)
            openTextDocument (text, this.getVScodeLanguageName (this.activeCodeGenName));
    }

    static getCSS()
    {
        return `
        cmaj-codegen-panel {
            position: relative;
            display: flex;
            flex-flow: row nowrap;
            align-items: stretch;
            min-height: 30rem;
            margin-bottom: 6rem;
            padding-top: 0.5rem;
            overflow: hidden;
            resize: vertical;
        }

        .cmaj-codegen-tabs {
            user-select: none;
            -webkit-user-select: none;
            -moz-user-select: none;
            -ms-user-select: none;
            display: flex;
            flex-flow: column nowrap;
            align-items: stretch;
            align-self: flex-start;
            min-width: 8rem;
            min-height: 30rem;
        }

        .cmaj-codegen-tab {
            background-color: #00000030;
            color: var(--foreground);
            cursor: pointer;
            padding: 0 1rem;
            text-align: left;
            display: inline-block;
            font-size: 0.8rem;
            height: 3rem;
        }

        .cmaj-active-tab {
            background: #222;
        }

        .cmaj-codegen-listing {
            flex-grow: 2;
            border: none;
            display: block;
            width: 100%;
            background: none;
            color: #5e5;
            background: #222;
        }

        .cmaj-codegen-listing textarea {
            font-family: Monaco, Consolas, monospace;
            position: relative;
            border: none;
            outline: none;
            display: block;
            background: #222;
            color: #5e5;
            min-width: 100%;
            height: 100%;
            resize: none;
            padding-left: 1rem;
        }

        .cmaj-open-codegen-button {
            display: block;
            position: absolute;
            right: 3%;
            height: 2rem;
            top: 1.5rem;
            z-index: 3;
        }`;
    }
}

//==============================================================================
export default class PatchPanel  extends HTMLElement
{
    constructor()
    {
        super();

        this.root = this.attachShadow ({ mode: "open" });

        this.session = cmajor.createServerSession (this.getSessionID());
        this.patchConnection = null;
        this.isSessionConnected = false;

        this.root.innerHTML = this.getHTML();

        this.statusListener = status => this.updateStatus (status);
        this.session.addStatusListener (this.statusListener);

        this.fileChangeListener = message => this.handlePatchFilesChanged (message);
        this.session.addFileChangeListener (this.fileChangeListener);

        this.session.addInfiniteLoopListener (handleInfiniteLoopAlert);

        this.controlsContainer   = this.shadowRoot.getElementById ("cmaj-control-container");
        this.logoElement         = this.shadowRoot.getElementById ("cmaj-logo")
        this.viewHolderElement   = this.shadowRoot.getElementById ("cmaj-patch-view-holder");
        this.viewSelectorElement = this.shadowRoot.getElementById ("cmaj-view-selector");
        this.toggleAudioButton   = this.shadowRoot.getElementById ("cmaj-toggle-audio-button");
        this.unloadButton        = this.shadowRoot.getElementById ("cmaj-unload-button");
        this.resetButton         = this.shadowRoot.getElementById ("cmaj-reset-button");
        this.copyStateButton     = this.shadowRoot.getElementById ("cmaj-copy-state");
        this.restoreStateButton  = this.shadowRoot.getElementById ("cmaj-restore-state");
        this.statusElement       = this.shadowRoot.getElementById ("cmaj-patch-status");
        this.inputsPanel         = this.shadowRoot.getElementById ("cmaj-inputs-panel");
        this.outputsPanel        = this.shadowRoot.getElementById ("cmaj-outputs-panel");
        this.cpuElement          = this.shadowRoot.getElementById ("cmaj-cpu");
        this.graphElement        = this.shadowRoot.getElementById ("cmaj-graph");
        this.errorListElement    = this.shadowRoot.getElementById ("cmaj-error-list");
        this.audioDevicePanel    = this.shadowRoot.getElementById ("cmaj-audio-device-panel");
        this.codeGenPanel        = this.shadowRoot.getElementById ("cmaj-codegen-panel");
        this.availablePatchList  = this.shadowRoot.getElementById ("cmaj-available-patch-list-holder");
        this.availablePatches    = this.shadowRoot.getElementById ("cmaj-available-patch-list");

        this.logoElement.onclick = () => openURLInNewWindow ("https://cmajor.dev");
        this.toggleAudioButton.onclick = () => this.toggleAudio();
        this.unloadButton.onclick = () => this.unloadPatch();
        this.resetButton.onclick = () => this.resetPatch();
        this.copyStateButton.onclick = () => this.copyState();
        this.restoreStateButton.onclick = () => this.restoreState();

        if (! this.isShowingFixedPatch())
        {
            const main = this.shadowRoot.querySelector (".cmaj-main");
            main.ondragover = e => this.handleDragOver (e);
            main.ondrop = e => this.handleDragAndDrop (e);
        }

        this.cpuElement.setSession (this.session);
        this.graphElement.setSession (this.session);
        this.codeGenPanel.setSession (this.session);

        this.initAccordionButtons();
        this.session.requestSessionStatus();
    }

    dispose()
    {
        unloadPatch();
        this.session.removeFileChangeListener (this.fileChangeListener);
        this.session.removeStatusListener (this.statusListener);
        this.session.dispose();
    }

    static get observedAttributes()
    {
        return ["session-id", "fixed-patch"];
    }

    disconnectedCallback()
    {
        this.unloadPatch();
    }

    getSessionID()
    {
        let sessionID = this.getAttribute("session-id");

        if (! sessionID)
        {
            sessionID = "";

            for (let i = 0; i < 3; ++i)
                sessionID += Math.floor (Math.random() * 65536).toString (16);
        }

        return sessionID;
    }

    loadPatch (patchURL)
    {
        if (patchURL && typeof patchURL == "string")
            this.session.loadPatch (patchURL);
        else
            this.unloadPatch();
    }

    unloadPatch()
    {
        this.viewHolderElement.innerHTML = "";
        this.session.loadPatch (null);
    }

    resetPatch()
    {
        this.session.setAudioPlaybackActive (true);
        this.patchConnection?.resetToInitialState();
    }

    isShowingFixedPatch()
    {
        return this.getAttribute("fixed-patch");
    }

    updateAvailablePatches (availablePatches)
    {
        this.availablePatches.innerHTML = "";
        this.availablePatchList.style.display = availablePatches?.length > 0 ? "inline-block" : "none";

        for (const manifest of availablePatches)
        {
            const button = document.createElement ("button");
            button.innerText = manifest.name || manifest.manifestFile;
            button.onclick = () =>
            {
                this.unloadPatch();
                this.loadPatch (manifest.manifestFile);
            }

            this.availablePatches.appendChild (button);
        }
    }

    toggleAudio()
    {
        this.session.setAudioPlaybackActive (! this.audioActive);
    }

    handleMessageFromVSCode (message)
    {
        if (message?.patchToLoad)
            this.loadPatch (message.patchToLoad);
        else if (message?.clipboardText)
            this.handleClipboardContent (message.clipboardText);
        else if (message?.unload)
            this.unloadPatch();
        else if (message?.reloaded)
            this.session?.requestSessionStatus(); // after a reload, VScode needs an update
    }

    disposePatchConnection()
    {
        if (this.patchConnection)
        {
            this.patchConnection.dispose();
            this.patchConnection = undefined;
        }
    }

    //==============================================================================
    updateStatus (status)
    {
        if (status.connected != this.isSessionConnected)
        {
            this.isSessionConnected = !! status.connected;

            if (this.isSessionConnected && ! this.isShowingFixedPatch())
                this.session.requestAvailablePatchList (list => this.updateAvailablePatches (list));
        }

        if (status.loaded && ! this.patchConnection)
        {
            this.patchConnection = this.session.createPatchConnection();
            this.refreshViewElement();
            this.graphElement.refresh();
            this.initAudioDevicePanel();
            this.codeGenPanel.refreshCodeGenTabs (status);
        }
        else if (this.patchConnection && ! status.loaded)
        {
            this.disposePatchConnection();
            this.refreshViewElement();
            this.graphElement.refresh();
            this.codeGenPanel.refreshCodeGenTabs (status);
        }

        this.populateInputsPanel (status);
        this.populateOutputsPanel (status);
        this.statusElement.innerHTML = this.getStatusHTML (status).trim();
        this.updateErrorList (status.error);
        this.updateViewSelectorList (status.loaded);

        this.audioActive = status.playing;
        this.toggleAudioButton.innerText = (status.playing ? "Stop Audio" : "Start Audio");

        this.toggleAudioButton.style.display = status.loaded ? "inline" : "none";
        this.unloadButton.style.display = (! this.isShowingFixedPatch() && status.loaded) ? "inline" : "none";
        this.copyStateButton.style.display = status.loaded ? "inline" : "none";
        this.restoreStateButton.style.display = status.loaded ? "inline" : "none";

        this.controlsContainer.style.display = status.loaded ? "block" : "none";
        this.cpuElement.style.display = (status.playing && status.loaded) ? "inline-block" : "none";

        window.sendMessageToVSCode?.({ newServerStatus: status });
    }

    populateInputsPanel (status)
    {
        while (this.inputsPanel.firstChild)
            this.inputsPanel.removeChild (this.inputsPanel.lastChild);

        const inputs = status.details?.inputs;
        let anyAdded = false;

        if (inputs && this.patchConnection)
        {
            for (let e of inputs)
            {
                const control = this.createControlForInputEndpoint (e);

                if (control)
                {
                    this.inputsPanel.appendChild (control);
                    anyAdded = true;
                }
            }
        }

        if (! anyAdded)
            this.inputsPanel.innerHTML = "<p>This processor has no input endpoints</p>";
    }

    populateOutputsPanel (status)
    {
        while (this.outputsPanel.firstChild)
            this.outputsPanel.removeChild (this.outputsPanel.lastChild);

        const outputs = status.details?.outputs;

        if (outputs && this.patchConnection)
        {
            for (let e of outputs)
            {
                const control = this.createControlForOutputEndpoint (e);

                if (control)
                    this.outputsPanel.appendChild (control);
            }
        }
    }

    createControlForInputEndpoint (e)
    {
        switch (e.purpose)
        {
            case "midi in":            return new MIDIInputControl (this.patchConnection, e);
            case "audio in":           return new AudioInputControl (this.patchConnection, e);
            case "parameter":          return undefined;
            case "time signature":     return undefined;
            case "tempo":              return undefined;
            case "transport state":    return undefined;
            case "timeline position":  return undefined;
            default:                   return new ConsoleEventControl (this.patchConnection, e);
        }
    }

    createControlForOutputEndpoint (e)
    {
        switch (e.purpose)
        {
            case "audio out":        return new AudioLevelControl (this.patchConnection, e);
            default:                 return new ConsoleEventControl (this.patchConnection, e);
        }
    }

    initAudioDevicePanel()
    {
        this.audioDevicePanel.innerHTML = "";
        this.audioDevicePanel.appendChild (new AudioDevicePropertiesPanel (this.session));
    }

    writeToClipboard (value)
    {
        try
        {
            const text = JSON.stringify (value);

            if (window.sendMessageToVSCode)
                window.sendMessageToVSCode ({ writeClipboard: text });
            else
                navigator.clipboard.writeText (text);
        }
        catch (e) {}
    }

    copyState()
    {
        this.patchConnection?.requestFullStoredState (state => this.writeToClipboard (state));
    }

    async restoreState()
    {
        try
        {
            if (window.sendMessageToVSCode)
                window.sendMessageToVSCode ({ readClipboard: true });
            else
                this.handleClipboardContent (await navigator.clipboard.readText());
        }
        catch (e) {}
    }

    handleClipboardContent (clipboardText)
    {
        if (clipboardText && this.patchConnection)
        {
            try
            {
                const state = JSON.parse (clipboardText);

                if (state)
                    this.patchConnection.sendFullStoredState (state);
            }
            catch (e) {}
        }
    }

    getStatusHTML (status)
    {
        let name = undefined;

        if (status.manifest?.name)
        {
            if (status.manifestFile)
                name = `<a href="${createFileLink (status.manifestFile)}">${status.manifest.name}</a>`;
            else
                name = status.manifest.name;
        }

        if (! name)
            name = status.manifestFile;

        if (! name)
            name = "unknown";

        if (status.loaded && status.details)
        {
            let text = ""

            const addDescriptionItem = (label, content) =>
            {
                if (content)
                {
                    if (typeof content == "string" && content.length == 0)
                        return;

                    text += `<p>${label}: ${content}</p>`;
                }
            };

            addDescriptionItem ("Loaded", name);
            addDescriptionItem ("Description", status.manifest?.description);
            addDescriptionItem ("Version", status.manifest?.version);
            addDescriptionItem ("Manufacturer", status.manifest?.manufacturer);
            addDescriptionItem ("Category", status.manifest?.category);

            if (status.details.mainProcessor)
            {
                let name = status.details.mainProcessor;

                if (status.details.mainProcessorLocation)
                    name = `<a href="${createFileLink (status.details.mainProcessorLocation)}">${name}</a>`;

                addDescriptionItem ("Main processor", name);
            }

            let audioOuts = 0, audioIns = 0;

            for (let e of status.details.outputs)
                if (e.purpose && e.purpose == "audio out")
                    audioOuts += e.numAudioChannels;

            for (let e of status.details.inputs)
                if (e.purpose && e.purpose == "audio in")
                    audioIns += e.numAudioChannels;

            const midiIns  = countEndpointsWithPurpose (status.details.inputs,  "midi in");
            const midiOuts = countEndpointsWithPurpose (status.details.outputs, "midi out");

            if (audioOuts + audioIns > 0)
            {
                let desc = "";

                if (audioIns > 0)
                    desc += audioIns + " input channel" + (audioIns != 1 ? "s" : "");

                if (audioOuts > 0)
                {
                    if (audioIns > 0)
                        desc += ", ";

                    desc += audioOuts + " output channel" + (audioOuts != 1 ? "s" : "");
                }

                addDescriptionItem ("Audio", desc);
            }

            if (midiIns + midiOuts > 0)
            {
                let desc = "";

                if (midiIns > 0)
                    desc += midiIns + " input" + (midiIns != 1 ? "s" : "");

                if (midiOuts > 0)
                {
                    if (midiIns > 0)
                        desc += ", ";

                    desc += midiOuts + " output" + (midiOuts != 1 ? "s" : "");
                }

                addDescriptionItem ("MIDI", desc);
            }

            const numParameters = countEndpointsWithPurpose (status.details.inputs, "parameter");

            if (numParameters > 0)
                addDescriptionItem ("Parameters", numParameters.toString());

            const numTimelineInputs = countEndpointsWithPurpose (status.details.inputs, "time signature")
                                    + countEndpointsWithPurpose (status.details.inputs, "tempo")
                                    + countEndpointsWithPurpose (status.details.inputs, "transport state")
                                    + countEndpointsWithPurpose (status.details.inputs, "timeline position");

            if (numTimelineInputs > 0)
                text += `<p>Uses timeline events</p>`;

            return text;
        }

        if (status.error)
            return `<p>Error when building: ${name}</p>`;

        if (status.status)
            return status.status;

        if (status.connected)
            return "No patch loaded";

        return "Connecting to server...";
    }

    onStatusChange (status) {}

    handlePatchFilesChanged (message)
    {
        if (message.assetFilesChanged || message.manifestChanged)
        {
            window.sendMessageToVSCode?.({ reloading: true });
            document.location.reload();
        }
    }

    handleDragOver (e)
    {
        e.preventDefault();
    }

    async getDragAndDroppedFiles (e, maxNumItems)
    {
        let items = [];
        let tooManyItems = false;

        const addItem = async (item) =>
        {
            if (item.isDirectory)
            {
                const reader = item.createReader();

                const getNextBatch = () => new Promise ((resolve, reject) =>
                {
                    reader.readEntries (resolve, reject);
                });

                for (;;)
                {
                    if (items.length >= maxNumItems)
                    {
                        tooManyItems = true;
                        return;
                    }

                    const entries = await getNextBatch();

                    if (entries.length === 0)
                        break;

                    for (const e of entries)
                        await addItem (e);
                }
            }
            else if (item.isFile)
            {
                if (items.length < maxNumItems)
                    items.push (item);
                else
                    tooManyItems = true;
            }
        }

        for (const item of e.dataTransfer?.items)
        {
            const entry = item.getAsEntry?.() || item.webkitGetAsEntry?.();

            if (tooManyItems || ! entry)
                return [];

            await addItem (entry);
        }

        return items;
    }

    getDroppedFilePromises (e, files)
    {
        let promises = [];

        for (const file of files)
        {
            promises.push (new Promise ((resolve, reject) =>
            {
                file.file (f =>
                {
                    const reader = new FileReader();

                    reader.onloadend = (e) =>
                    {
                        resolve ({ file: file,
                                   size: e.total,
                                   content: e.target.result
                                 });
                    }

                    reader.readAsArrayBuffer (f);
                });
            }));
        }

        return promises;
    }

    async handleDragAndDrop (e)
    {
        e.preventDefault();

        const files = await this.getDragAndDroppedFiles (e, 200);

        if (files?.length == 0)
            return;

        let manifest = null;
        let readers = [];

        for (const p of this.getDroppedFilePromises (e, files))
        {
            const reader = await p;

            if (! reader)
                return;

            readers.push (reader);

            if (reader.file.fullPath.endsWith (".cmajorpatch"))
                manifest = reader.file.fullPath;
        }

        if (manifest)
        {
            const prefix = "/uploaded-session-files";

            for (const reader of readers)
            {
                this.session.registerFile (prefix + reader.file.fullPath, {
                    size: reader.size,
                    read: (offset, numBytes) =>
                    {
                        const data = reader.content.slice (offset, offset + numBytes);
                        return new Blob ([data]);
                    }
                });
            }

            this.loadPatch (prefix + manifest);
        }
    }

    async refreshViewElement (viewType)
    {
        if (this.patchConnection)
        {
            const view = await patchViewUtils.createPatchViewHolder (this.patchConnection, viewType);

            this.viewHolderElement.innerHTML = "";

            if (view)
                this.viewHolderElement.appendChild (view);
        }
        else
        {
            this.viewHolderElement.innerHTML = "";
        }
    }

    updateErrorList (errorText)
    {
        this.errorListElement.style.display = errorText ? "block" : "none";
        let list = "";

        if (errorText)
        {
            const lines = errorText.toString().split ("\n");

            for (let line of lines)
            {
                let i = line.indexOf (": error:");
                if (i < 0) i = line.indexOf (": warning:");
                if (i < 0) i = line.indexOf (": note:");

                if (i >= 0)
                {
                    const file = line.substring (0, i);
                    line = `<a href="${createFileLink (file)}">${file}</a>${line.substring (i)}`;
                }

                list += `<p>${line}</p>`;
            }
        }

        this.errorListElement.innerHTML = list;
    }

    updateViewSelectorList (isLoaded)
    {
        const viewSelector = this.viewSelectorElement;
        const availableViews = patchViewUtils.getAvailableViewTypes (this.patchConnection);

        if (isLoaded && availableViews && availableViews.length > 1)
        {
            let items = "";

            for (let viewName of availableViews)
                items += `<option value="${viewName}">${viewName}</option>`;

            viewSelector.innerHTML = items;
            viewSelector.style.display = "block";

            viewSelector.onchange = async (event) =>
            {
                const index = viewSelector.selectedIndex;

                if (index >= 0 && index < availableViews.length)
                    this.refreshViewElement (availableViews[index]);
            };
        }
        else
        {
            while (viewSelector.firstChild)
                viewSelector.removeChild (viewSelector.lastChild);

            viewSelector.style.display = "none"
            viewSelector.onchange = undefined;
        }
    }

    //==============================================================================
    initAccordionButtons()
    {
        for (let button of this.shadowRoot.querySelectorAll (".cmaj-accordion-button"))
        {
            function updatePanelSize (button)
            {
                const panel = button.nextElementSibling;

                if (button.classList.contains ("cmaj-accordian-open"))
                    panel.style.maxHeight = "400rem";
                else
                    panel.style.maxHeight = "0rem";
            }

            updatePanelSize (button);

            button.onclick = (event) =>
            {
                if (event.target == button)
                {
                    const panel = button.nextElementSibling;

                    if (button.classList.contains ("cmaj-accordian-open"))
                        button.classList.remove ("cmaj-accordian-open");
                    else
                        button.classList.add ("cmaj-accordian-open");

                    updatePanelSize (button);
                }
            };
        }
    }

    //==============================================================================
    getHTML()
    {
        return `
<style>
    * {
        box-sizing: border-box;
        font-family: Monaco, Consolas, monospace;
        font-size: 95%;
    }

    :host {
        --background: #445550;
        --foreground: #dddddd;
        --header-height: 6rem;

        background: var(--background);
        display: block;
    }

    .cmaj-main {
        min-height: 100vh;
    }

    .cmaj-header {
        width: 100%;
        min-height: 6rem;
        font-size: 0.9rem;
        padding: 0.5rem;
        padding-top: 0.6rem;
        margin-bottom: 0.6rem;

        display: flex;
        flex-flow: row nowrap;
        justify-content: left;
        align-items: stretch;
        overflow: hidden;
    }

    .cmaj-header p {
        margin-block-start: 0;
        margin-block-end: 0;
    }

    a {
        color: var(--foreground);
    }

    .cmaj-logo-and-version-holder {
        display: flex;
        flex-flow: column nowrap;
        align-items: center;
        margin-right: 1.2rem;
    }

    .cmajor-logo {
        user-select: none;
        -webkit-user-select: none;
        -moz-user-select: none;
        -ms-user-select: none;
        min-width: 8rem;
        min-height: 4rem;
        align-self: flex-center;
        background-color: var(--foreground);
        mask: url(/cmaj_api/assets/cmajor-logo.svg);
        mask-repeat: no-repeat;
        mask-position: center;
        -webkit-mask: url(/cmaj_api/assets/cmajor-logo.svg);
        -webkit-mask-repeat: no-repeat;
        -webkit-mask-position: center;
        cursor: pointer;
    }

    .cmaj-version-number {
        color: #999;
        font-size: 0.7rem;
        padding: 0 0 1rem 0;
        margin: 0;
    }

    button {
        border: none;
        padding: 0.2rem 0.6rem;
        background-color: #ccccff30;
        color: #ddd;
        border-radius: 0.3rem;
    }

    button:hover {
        background-color: #ddddff70;
    }

    .cmaj-selected-button {
        background-color: #44ee4450;
    }

    .cmaj-selected-button:hover {
        background-color: #44ee4480;
    }

    select {
        background-color: #ccccff30;
        color: #ddd;
        padding: 0.3rem;
        margin: 0;
        border-radius: 0.3rem;
    }

    select:hover {
        background-color: #ddddff60;
    }

    #cmaj-patch-status {
        color: var(--foreground);
        flex: 2;
        height: 100%;
        overflow: hidden;
        text-overflow: ellipsis;
        padding-left: 0.5rem;
        margin-block-start: 0;
        margin-block-end: 0;
    }

    .cmaj-top-panel-buttons {
        display: flex;
        flex-flow: column nowrap;
        justify-content: space-between;
        align-items: flex-end;
    }

    .cmaj-top-panel-buttons button {
        margin-left: 1rem;
    }

    cmaj-cpu-meter {
        width: 6rem;
        height: 1.5rem;
        align-self: flex-center;
    }

    .cmaj-divider {
        width: 100%;
        height: 0.05rem;
        border-top: 0.05rem solid var(--foreground);
        margin: 0;
        padding: 0;
    }

    .cmaj-control-container {
        padding: 0.5rem 0.2rem;
        margin: 0 0.6rem;
    }

    #cmaj-available-patch-list-holder {
        background: solid;
        background-color: #00000044;
        padding-top: 0.5rem;
        margin: 0;
    }

    #cmaj-available-patch-list button {
        background: none;
        cursor: pointer;
        margin: 0 0 0.3rem 0.3rem;
    }

    #cmaj-available-patch-list button:hover {
        background-color: #ddddff40;
        cursor: pointer;
    }

    .cmaj-io-panel {
        display: flex;
        padding: 0.5rem 0;
        flex-flow: row wrap;
        justify-content: center;
        align-items: center;
    }

    cmaj-panel-piano-keyboard {
        display: inline-block;
        height: 6rem;
        min-width: 10rem;
        flex-basis: 20rem;
        flex-grow: 1;
    }

    #cmaj-patch-view-holder {
        position: relative;
        display: block;
        overflow: auto;
        padding-top: 0.3rem;
        padding-bottom: 0.5rem;
        resize: vertical;
        height: 400px;
    }

    #cmaj-graph {
        max-height: 50rem;
    }

    #cmaj-error-list {
        display: block;
        white-space: pre-wrap;
        font-size: 0.9rem;
        min-height: 18rem;
        background: #222222;
        color: #bbbbbb;
        padding: 0.8rem;
        margin: 0.8rem;
        overflow: auto;
        resize: vertical;
    }

    #cmaj-error-list p {
        margin: 0;
    }

    #cmaj-error-list a {
        color: #aaa;
    }

    .cmaj-action-button-holder {
        padding: 0.6rem 0;
    }

    .cmaj-action-button-holder button {
        margin-bottom: 0.6rem;
    }

    .cmaj-accordion-button {
        background-color: #ffffff40;
        color: var(--foreground);
        cursor: pointer;
        padding: 0 1rem;
        height: 2.5rem;
        width: 100%;
        border: none;
        outline: none;
        text-align: left;
        font-size: 1rem;
        transition: 0.2s;
        display: flex;
        justify-content: space-between;
        align-items: center;
        border-bottom: 0.1rem solid #00000033;
        border-radius: 0.1rem;
    }

    .cmaj-accordion-button:hover {
        background-color: #ffffff60;
    }

    .cmaj-accordion-panel {
        padding: 0 0.8rem;
        background-color: #ffffff20;
        overflow: hidden;
        transition: max-height 0.25s ease-out;
    }

    #cmaj-view-selector {
        font-size: 0.8rem;
    }

    ${AudioDevicePropertiesPanel.getCSS()}
    ${ConsoleEventControl.getCSS()}
    ${EndpointControlBase.getCSS()}
    ${CodeGenPanel.getCSS()}

</style>

<div class="cmaj-main">
  <div id="cmaj-available-patch-list-holder" hidden>
    <div id="cmaj-available-patch-list"></div>
    <div class="cmaj-divider"></div>
  </div>

  <div class="cmaj-header">
    <div class="cmaj-logo-and-version-holder">
      <span id="cmaj-logo" class="cmajor-logo"></span>
      <p class="cmaj-version-number">version ${getCmajorVersion()}</p>
    </div>
    <p id="cmaj-patch-status"></p>
    <cmaj-cpu-meter id="cmaj-cpu"></cmaj-cpu-meter>
    <div class="cmaj-top-panel-buttons">
      <button id="cmaj-toggle-audio-button">Stop Audio</button>
      <button id="cmaj-unload-button">Unload Patch</button>
    </div>
  </div>

  <div class="cmaj-divider"></div>

  <div class="cmaj-control-container" id="cmaj-control-container">
    <div class="cmaj-action-button-holder">
      <button id="cmaj-reset-button">Reset patch</button>
      <button id="cmaj-copy-state">Copy state to clipboard</button>
      <button id="cmaj-restore-state">Paste state from clipboard</button>
    </div>

    <button class="cmaj-accordion-button cmaj-accordian-open">GUI
      <select id="cmaj-view-selector"></select>
    </button>
    <div class="cmaj-accordion-panel">
      <div id="cmaj-patch-view-holder"></div>
    </div>

    <button class="cmaj-accordion-button cmaj-accordian-open">Inputs</button>
    <div class="cmaj-accordion-panel">
      <div id="cmaj-inputs-panel" class="cmaj-io-panel"></div>
    </div>

    <button class="cmaj-accordion-button cmaj-accordian-open">Outputs</button>
    <div class="cmaj-accordion-panel">
      <div id="cmaj-outputs-panel" class="cmaj-io-panel"></div>
    </div>

    <button class="cmaj-accordion-button">Graph</button>
    <div class="cmaj-accordion-panel">
      <cmaj-patch-graph id="cmaj-graph"></cmaj-patch-graph>
    </div>

    <button class="cmaj-accordion-button">Audio Device Settings</button>
    <div class="cmaj-accordion-panel">
      <div id="cmaj-audio-device-panel" class="cmaj-audio-device-panel"></div>
    </div>

    <button class="cmaj-accordion-button">Generated Code</button>
    <div class="cmaj-accordion-panel">
      <cmaj-codegen-panel id="cmaj-codegen-panel"></cmaj-codegen-panel>
    </div>

  </div>
  <pre id="cmaj-error-list"></pre>
</div>
</html>`;
    }
};

function countEndpointsWithPurpose (endpoints, purpose)
{
    let count = 0;

    if (endpoints)
        for (let e of endpoints)
            if (e.purpose && e.purpose == purpose)
                ++count;

    return count;
}

function printMIDI (message)
{
    const hex = "00000" + message.toString (16);
    const len = hex.length;

    return hex.substring (len - 6, len - 4)
            + " " + hex.substring (len - 4, len - 2)
            + " " + hex.substring (len - 2, len);
}

customElements.define ("cmaj-level-meter", LevelMeter);
customElements.define ("cmaj-waveform-display", WaveformDisplay);
customElements.define ("cmaj-panel-piano-keyboard", PianoKeyboard);

customElements.define ("cmaj-control-console", ConsoleEventControl);
customElements.define ("cmaj-control-in-midi", MIDIInputControl);
customElements.define ("cmaj-control-audio", AudioLevelControl);
customElements.define ("cmaj-control-audio-in", AudioInputControl);
customElements.define ("cmaj-codegen-panel", CodeGenPanel);

customElements.define ("cmaj-audio-device-panel", AudioDevicePropertiesPanel);
customElements.define ("cmaj-patch-panel", PatchPanel);
