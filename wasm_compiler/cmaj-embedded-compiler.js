//
//     ,ad888ba,                              88
//    d8"'    "8b
//   d8            88,dba,,adba,   ,aPP8A.A8  88
//   Y8,           88    88    88  88     88  88
//    Y8a.   .a8P  88    88    88  88,   ,88  88     (C)2024 Cmajor Software Ltd
//     '"Y888Y"'   88    88    88  '"8bbP"Y8  88     https://cmajor.dev
//                                           ,88
//                                        888P"
//
//  This file may be used under the terms of the ISC license:
//
//  Permission to use, copy, modify, and/or distribute this software for any purpose with or
//  without fee is hereby granted, provided that the above copyright notice and this permission
//  notice appear in all copies. THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
//  WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
//  AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
//  CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
//  WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
//  CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.


import CmajCompilerModule from "./cmaj-compiler-wasm.js";
import * as helpers from "./cmaj-audio-worklet-helper.js"

/**
 *   This class can be given a URL or some source file content for a Cmajor patch, and will
 *   compile a javascript/WASM object from i
 */
export default class CmajorCompiler
{
    constructor()
    {
        this.CmajorVersion = "unknown";
        this.baseURL = "";
        this.sources = [];
    }

    /** Sets a base URL and the relative path from this to a the .cmajorpatch manifest file
     *  of the patch that you'd like to compile.
     *  @param {URL} baseURL - The base URL for the patch folder
     *  @param {string} manifestPath - The relative path of the .cmajorpatch file from the base URL
     */
    setManifestURL (baseURL, manifestPath)
    {
        if (typeof baseURL !== 'URL')
            baseURL = new URL (baseURL);

        if (! baseURL.href.endsWith ("/"))
            baseURL = new URL (baseURL.href + "/");

        this.readPatchFile = async (path) =>
        {
            const url = new URL (path, baseURL);
            const response = await fetch (url);
            const content = new Uint8Array (await response.arrayBuffer());
            return content;
        };

        this.baseURL = baseURL;
        this.manifestPath = manifestPath;
    }

    /** Manually adds a source file at a given path from the root of the patch. If you
     *  use setManifestURL() then you don't need to call this, but to load a patch from
     *  memory, you can also call this to give it all the files it might need before building.
     *  @param {string} path - The path to this file, relative to the root of the patch
     *  @param {(string|Uint8Array)} content - The content of the file - either a string or a UInt8Array
     */
    addSourceFile (path, content)
    {
        for (const f of this.sources)
        {
            if (f.path == path)
            {
                f.content = content;
                return;
            }
        }

        this.sources.push ({ path, content });
    }

    //==============================================================================
    /** Attempts to build the patch, and returns an AudioWorkletNodePatchConnection object
     *  to control it if it succeeds.
     *  @param {AudioContext} audioContext - a web audio AudioContext object
     *  @param {string} workletName - the name to give the new worklet that is created
     */
    async createAudioWorkletNodePatchConnection (audioContext, workletName)
    {
        const MainClass = await this.createJavascriptWrapperClass();

        if (! MainClass)
            throw new Error ("Failed to load the compiled class");

        const manifest = await this.readManifest();
        const connection = new helpers.AudioWorkletPatchConnection (manifest);

        connection.getCmajorVersion = () => { return this.CmajorVersion; };

        await (connection.initialise ({ CmajorClass: MainClass,
                                        audioContext,
                                        workletName,
                                        hostDescription: "WebAudio",
                                        rootResourcePath: this.baseURL }));
        return connection;
    }

    //==============================================================================
    /** Attempts to build and return the Cmajor patch class as a javascript object. */
    async createJavascriptWrapperClass()
    {
        const code = await this.createJavascriptCode();
        let result = [];
        const f = new Function (`arguments[0].push (${code});`);
        await f (result);
        return result[0];
    }

    //==============================================================================
    /** Attempts to compile the patch into its javascript/WASM form, returning the
     *  resultant code as a string if it succeeds.
     */
    async createJavascriptCode()
    {
        const module = await CmajCompilerModule();
        this.CmajorVersion = module.UTF8ToString (module._getCmajorVersion());

        for (;;)
        {
            module._initialise (128);

            const toUint8Array = (source) =>
            {
                if (source instanceof Uint8Array)
                    return source;

                if (typeof source == 'string')
                {
                    const encoder = new TextEncoder();
                    return encoder.encode (source);
                }

                throw new Error (`The content type of source "${source}" was not supported`);
            }

            await this.addKnownFilesFromManifest();

            for (const source of this.sources)
            {
                const pathPtr = module.allocate (module.intArrayFromString (source.path), module.ALLOC_NORMAL);
                const content = toUint8Array (source.content);
                const contentPtr = module.allocate (content, module.ALLOC_NORMAL);

                module._addFile (pathPtr, contentPtr, content.length);

                module._free (pathPtr)
                module._free (contentPtr)
            }

            if (! module._generate())
            {
                const missingFiles = module.UTF8ToString (module._getUnresolvedResourceFiles());

                if (missingFiles?.length > 0)
                {
                    const list = JSON.parse (missingFiles);

                    if (list?.length > 0)
                    {
                        for (const file of list)
                            await this.getSourceFileContent (file);

                        continue;
                    }
                }

                throw new Error (module.UTF8ToString (module._getMessages()));
            }

            return module.UTF8ToString (module._getGeneratedCode());
        }
    }

    //==============================================================================
    /** Tries to read and return the content of a file. */
    async getSourceFileContent (path)
    {
        for (const f of this.sources)
            if (f.path == path)
                return f.content;

        let content;

        if (this.readPatchFile)
        {
            content = await this.readPatchFile (path);
            this.addSourceFile (path, content);
        }

        if (! content)
            throw new Error ("Could not read required file " + path);

        return content;
    }

    /** Tries to read and return the content of a file as a string. */
    async getSourceFileContentAsString (path)
    {
        const content = await this.getSourceFileContent (path);

        if (typeof content === "string")
            return content;

        const decoder = new TextDecoder('utf-8');
        return await decoder.decode (content);
    }

    /** Tries to read and return the manifest as a JSON object. */
    async readManifest()
    {
        const getManifestPath = () =>
        {
            if (this.manifestPath)
                return this.manifestPath;

            for (const f of this.sources)
                if (f.path.endsWith (".cmajorpatch"))
                    return f.path;

            throw new Error ("Couldn't find a manifest file");
        }

        return JSON.parse (await this.getSourceFileContentAsString (getManifestPath()));
    }

    /** @private */
    async addKnownFilesFromManifest()
    {
        const addSource = async (source) =>
        {
            if (source instanceof Array)
            {
                for (const s of source)
                    await addSource (s.toString());
            }
            else if (source)
            {
                const content = await this.getSourceFileContent (source.toString());
                this.addSourceFile (source.toString(), content);
            }
        }

        const manifest = await this.readManifest();

        await addSource (manifest.source);
        await addSource (manifest.patchworker);
    }
}
