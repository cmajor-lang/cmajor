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

/*
    Theming. The panel's colours are the --cp-* custom properties in theme/tokens.css (see
    scratch/patch-panel/DESIGN.md for the design system), with a dark and a light palette,
    chosen by the panel's data-theme attribute.

    - Inside VS Code, the extension posts its --vscode-* colours to the panel, and
      applyHostTheme() maps them onto the base --cp-* tokens, so the panel matches the editor.
      The derived tokens are mixed from the base ones, so they follow.
    - In a browser, the user can choose auto/light/dark; auto follows prefers-color-scheme.

    Canvas-drawn widgets can't use CSS variables directly, so getThemeColours() reads the
    resolved colours once per theme change and caches them.
*/

/// VS Code colour names → panel tokens. The first one that the host provides wins; a token
/// that none of them provides keeps its value from tokens.css.
const hostTokenMap = {
    "--cp-bg":          ["--vscode-editor-background"],
    "--cp-surface":     ["--vscode-sideBar-background", "--vscode-panel-background", "--vscode-editorWidget-background", "--vscode-editor-background"],
    "--cp-fg":          ["--vscode-editor-foreground", "--vscode-foreground"],
    "--cp-fg-muted":    ["--vscode-descriptionForeground"],
    "--cp-divider":     ["--vscode-panel-border", "--vscode-widget-border", "--vscode-editorGroup-border"],
    "--cp-accent":      ["--vscode-focusBorder", "--vscode-button-background"],
    "--cp-on-accent":   ["--vscode-button-foreground"],
    "--cp-indicator":   ["--vscode-panelTitle-activeBorder"],
    "--cp-link":        ["--vscode-textLink-foreground"],
    "--cp-err":         ["--vscode-errorForeground", "--vscode-editorError-foreground"],
    "--cp-warn":        ["--vscode-editorWarning-foreground"],
    "--cp-field":       ["--vscode-input-background", "--vscode-dropdown-background"],
    "--cp-field-border": ["--vscode-input-border", "--vscode-dropdown-border"],
    "--cp-hover":       ["--vscode-toolbar-hoverBackground", "--vscode-list-hoverBackground"],
    "--cp-sans":        ["--vscode-font-family"],
    "--cp-mono":        ["--vscode-editor-font-family"],
};

/// The --vscode-* names that hostTokenMap uses (the extension sends these)
export const hostThemeColourNames = [...new Set (Object.values (hostTokenMap).flat())];

/// Applies a theme sent by VS Code (an object of --vscode-* names → values, plus kind:
/// "light" | "dark" | "high-contrast" ...), or removes it if theme is null
export function applyHostTheme (element, theme)
{
    for (const token of Object.keys (hostTokenMap))
        element.style.removeProperty (token);

    if (! theme)
    {
        element.removeAttribute ("data-host-theme");
        return;
    }

    for (const [token, names] of Object.entries (hostTokenMap))
    {
        const value = names.map (n => theme[n]).find (v => typeof v === "string" && v.trim() !== "");

        if (value)
            element.style.setProperty (token, value.trim());
    }

    element.setAttribute ("data-host-theme", "");
    element.dataset.theme = String (theme.kind ?? "").includes ("light") ? "light" : "dark";
    invalidateThemeColours();
}

//==============================================================================
/// Keeps the panel's data-theme attribute in step with a browser preference
/// ("auto" | "light" | "dark"). Returns a function that stops following the system setting.
export function applyBrowserTheme (element, preference)
{
    const query = globalThis.matchMedia?.("(prefers-color-scheme: light)");

    const update = () =>
    {
        if (element.hasAttribute ("data-host-theme"))
            return;

        const isLight = preference === "light" || (preference !== "dark" && !! query?.matches);
        element.dataset.theme = isLight ? "light" : "dark";
        invalidateThemeColours();
    };

    update();
    query?.addEventListener?.("change", update);
    return () => query?.removeEventListener?.("change", update);
}

//==============================================================================
let cachedColours = new WeakMap();     // theme root element → its colours
let themeVersion = 0;
const themeListeners = new Set();

export function invalidateThemeColours()
{
    cachedColours = new WeakMap();
    ++themeVersion;

    for (const l of [...themeListeners])
        l();
}

/// Calls listener() whenever the theme changes. Returns a function that removes it.
export function addThemeChangeListener (listener)
{
    themeListeners.add (listener);
    return () => themeListeners.delete (listener);
}

export function getThemeVersion()       { return themeVersion; }

/// Returns the panel's resolved colours (for canvas drawing), from the given element's styles.
/// They're cached per theme root (the panel, or a .cp-theme element), so two differently themed
/// panels on one page each get their own.
export function getThemeColours (element)
{
    if (! element?.isConnected)
        return fallbackColours;

    const root = element.closest?.("cmaj-patch-panel, .cp-theme") ?? element;
    let colours = cachedColours.get (root);

    if (! colours)
    {
        // (a custom property may hold e.g. a color-mix(), so resolve each one through a probe
        // element's computed colour, which is always plain rgb. The probe goes in the theme root,
        // where the tokens are set: an element that's e.g. waiting to be slotted into a shadow
        // tree has no computed style at all)
        const probe = document.createElement ("span");
        probe.style.display = "none";
        root.appendChild (probe);
        let isResolved = true;

        const get = (name, fallback) =>
        {
            probe.style.color = fallback;
            probe.style.color = `var(${name}, ${fallback})`;
            const colour = getComputedStyle (probe).color;

            if (! colour)
                isResolved = false;

            return colour || fallback;
        };

        colours = Object.fromEntries (Object.entries (fallbackColours)
                                        .map (([key, fallback]) => [key, get (canvasColourTokens[key], fallback)]));

        probe.remove();

        // (try again next time, rather than keep the fallbacks)
        if (isResolved)
            cachedColours.set (root, colours);
    }

    return colours;
}

/// The tokens that canvas widgets draw with (see getThemeColours())
const canvasColourTokens = {
    fg:             "--cp-fg",
    fgMuted:        "--cp-fg-muted",
    divider:        "--cp-divider",
    meterTrack:     "--cp-meter-track",
    meterLow:       "--cp-meter-low",
    meterHigh:      "--cp-meter-high",
    meterClip:      "--cp-meter-clip",
    meterPeak:      "--cp-meter-peak",
    scopeTrace:     "--cp-scope-trace",
    cpuTrace:       "--cp-cpu-trace",
    cpuPeak:        "--cp-cpu-peak",
    cpuOver:        "--cp-cpu-over",
    spectrumBg:     "--cp-spectrum-bg",
    spectrumGrid:   "--cp-spectrum-grid",
    spectrumLabel:  "--cp-spectrum-label",
    spectrumTrace:  "--cp-spectrum-trace",
    spectrumPeak:   "--cp-spectrum-peak",
    spectrumCursor: "--cp-spectrum-cursor",
    waveBg:              "--cp-wave-bg",
    waveGrid:            "--cp-wave-grid",
    waveLabel:           "--cp-wave-label",
    waveTrace:           "--cp-wave-trace",
    waveCursor:          "--cp-wave-cursor",
    waveSelection:       "--cp-wave-selection",
    waveSelectionEdge:   "--cp-wave-selection-edge",
    waveDropout:         "--cp-wave-dropout",
    waveDropoutHatch:    "--cp-wave-dropout-hatch",
    waveMarker:          "--cp-wave-marker",
    waveMarkerEvent:     "--cp-wave-marker-event",
    waveMarkerParameter: "--cp-wave-marker-parameter",
};

/// (the dark palette, for when there's no panel element to read the tokens from)
const fallbackColours = {
    fg:             "#e2e3e5",
    fgMuted:        "#989a9f",
    divider:        "#2d2f33",
    meterTrack:     "#2c2d30",
    meterLow:       "#4cc38a",
    meterHigh:      "#e2a64a",
    meterClip:      "#ef6b5e",
    meterPeak:      "#e2e3e5",
    scopeTrace:     "#3fbfae",
    cpuTrace:       "#989a9f",
    cpuPeak:        "#e2a64a",
    cpuOver:        "#ef6b5e",
    spectrumBg:     "#2c2d30",
    spectrumGrid:   "#3d3e42",
    spectrumLabel:  "#6e7075",
    spectrumTrace:  "#3fbfae",
    spectrumPeak:   "#989a9f",
    spectrumCursor: "#e2e3e5",
    waveBg:              "#2c2d30",
    waveGrid:            "#3d3e42",
    waveLabel:           "#6e7075",
    waveTrace:           "#3fbfae",
    waveCursor:          "#e2e3e5",
    waveSelection:       "rgba(77, 155, 245, 0.22)",
    waveSelectionEdge:   "#4d9bf5",
    waveDropout:         "rgba(226, 166, 74, 0.2)",
    waveDropoutHatch:    "rgba(226, 166, 74, 0.55)",
    waveMarker:          "#4d9bf5",
    waveMarkerEvent:     "#a08cff",
    waveMarkerParameter: "#e2b34a",
};
