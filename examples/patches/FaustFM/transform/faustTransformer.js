
class CmajorCompiler {
    fModule;
    fCompiler;
    fFS;
    fErrorMessage;
    fErrorLine;
    fErrorCol;

    // Initializes the class with necessary WebAssembly module components
    constructor(module) {
        this.fModule = module;
        this.fCompiler = new module.libFaustWasm();
        this.fFS = module.FS;
        this.fErrorMessage = "";
        this.fErrorLine = 0;
        this.fErrorCol = 0;
    };

    // Returns the version of the FAUST library being used
    getLibFaustVersion() {
        return this.fCompiler.version();
    };

    // Retrieves the last compilation error message
    getErrorMessage() {
        return this.fErrorMessage;
    };

    getErrorLine() {
        return this.fErrorLine;
    }

    getErrorCol() {
        return this.fErrorCol;
    }

    /**
     * Compiles a FAUST program with specified parameters.
     *
     * @param {string} dsp_name - The name of the DSP program.
     * @param {string} dsp_content - The FAUST program code.
     * @param {string} argv - Additional arguments for the compiler as a string.
     * @returns {string|null} The compiled program, or null if compilation fails.
     */
    compile(dsp_name, dsp_content, argv) {
        try {
            // Customize the compilation arguments
            argv = argv + "-lang cmajor-hybrid -cn " + dsp_name + " -o foo.cmajor";
            const res = this.fCompiler.generateAuxFiles(dsp_name, dsp_content, argv);
            return (res) ? this.fFS.readFile("foo.cmajor", { encoding: "utf8" }) : null;
        }
        catch (e) {
            // Enhanced error handling to provide more detailed feedback
            this.fErrorMessage = this.fCompiler.getErrorAfterException() || e.toString();
            this.fCompiler.cleanupAfterException();
            return null;
        }
    };
};

class SouceTransformer {
    constructor(cmajor) {
        window.currentView = this;
        this.cmajor = cmajor;
        this.sendMessageToServer({ type: "ready" });
    }

    sendMessageToServer(message) {
        cmaj_sendMessageToServer(message);
    }

    deliverMessageFromServer(msg) {
        if (msg.type == "transformRequest") {
            this.transform(msg.message.requestId, msg.message.filename, msg.message.contents);
        }
    }

    transform(requestId, filepath, contents) {
        if (filepath.endsWith(".dsp")) {
            let filename = filepath.substring(filepath.lastIndexOf('/') + 1);
            let prefix = filename.substr(0, filename.length - 4);
            let code = this.cmajor.compile(prefix, contents, "");

            if (code == null) {
                this.sendTransformError(requestId,
                    filename,
                    this.cmajor.getErrorMessage(),
                    this.cmajor.getErrorLine(),
                    this.cmajor.getErrorCol());
            }
            else {
                this.sendTransformResponse(requestId, filename, code);
            }
        }
        else {
            this.sendTransformResponse(requestId, filepath, contents);
        }
    }

    sendTransformResponse(requestId, filename, transformedContents) {
        this.sendMessageToServer
            ({
                type: "transformResponse",
                message: { requestId: requestId, contents: transformedContents }
            });
    }

    sendTransformError(requestId, filename, desc, l, c) {
        this.sendMessageToServer
            ({
                type: "transformError",
                message: { requestId: requestId, description: desc, line: l, column: c }
            });
    }
}

export default async function runWorker() {
    // Uing the bundled version of the Faust library
    const { instantiateFaustModule } = await import("./faustModule.js");
    let module = await instantiateFaustModule();
    let cmajor = new CmajorCompiler(module);

    const connection = new SouceTransformer(cmajor);
}
