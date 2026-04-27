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

// Shared type declarations for the Cmajor JavaScript API.

interface EndpointAnnotation {
    min?: number;
    max?: number;
    mid?: number;
    step?: number;
    init?: number;
    unit?: string;
    boolean?: boolean;
    discrete?: boolean;
    text?: string;
    hidden?: boolean;
    name?: string;
}

interface EndpointInfo {
    endpointID: string;
    endpointType: string;
    dataType?: unknown;
    purpose: string;
    numAudioChannels?: number;
    defaultValue?: number;
    name?: string;
    annotation?: EndpointAnnotation;
}

interface PatchManifest {
    view?: { src?: string; width?: number; height?: number };
    name?: string;
    description?: string;
    worker?: string;
}

interface StatusMessage {
    connected: boolean;
    loaded: boolean;
    manifest?: PatchManifest;
    details?: { inputs: EndpointInfo[]; outputs: EndpointInfo[] };
    sampleRate?: number;
    host?: string;
    httpRootURL?: string;
    status?: string;
}
