cmake_minimum_required(VERSION 3.17)

project(
    cmajor_compiler
    VERSION 0.1
    LANGUAGES CXX C)

add_executable(cmajor_compiler)

file(GLOB_RECURSE SOURCE
    ../../modules/compiler/src/*.h
    ../../modules/compiler/src/*.cpp
    ../../3rdParty/graphviz/cmaj_GraphViz.cpp
)

target_sources(cmajor_compiler
    PRIVATE
        cmaj_WebAssemblyCompiler.cpp
        ${SOURCE}
)

set_target_properties(cmajor_compiler PROPERTIES OUTPUT_NAME "cmaj-compiler-wasm")

include (../scripts/cmake_warning_flags)

target_compile_features(cmajor_compiler PRIVATE ${CMAJ_TARGET_COMPILER})
target_compile_options(cmajor_compiler PRIVATE ${CMAJ_WARNING_FLAGS})

# include (../../cmajor_version)

add_compile_definitions (
    CMAJ_VERSION="${CMAJ_VERSION}"
    CMAJ_ENABLE_PERFORMER_LLVM=1
    CMAJ_ENABLE_PERFORMER_WEBVIEW=0
    CMAJ_ENABLE_PERFORMER_CPP=0
    CMAJ_ENABLE_CODEGEN_CPP=0
    CMAJ_ENABLE_CODEGEN_LLVM_WASM=1
    CHOC_ASSERT=must_include_assertion_header_before_any_choc_headers
)

set (EXTRA_LIBS

    LLVMWindowsManifest
    LLVMXRay
    LLVMLibDriver
    LLVMDlltoolDriver
    LLVMTextAPIBinaryReader
    LLVMCoverage
    LLVMLineEditor

    LLVMWebAssemblyDisassembler
    LLVMWebAssemblyAsmParser
    LLVMWebAssemblyCodeGen
    LLVMWebAssemblyUtils
    LLVMWebAssemblyDesc
    LLVMWebAssemblyInfo

    LLVMOrcDebugging
    LLVMOrcJIT
    LLVMWindowsDriver
    LLVMMCJIT
    LLVMJITLink
    LLVMInterpreter
    LLVMExecutionEngine
    LLVMRuntimeDyld
    LLVMOrcTargetProcess
    LLVMOrcShared
    LLVMDWP
    LLVMDebugInfoLogicalView
    LLVMDebugInfoGSYM
    LLVMOption
    LLVMObjectYAML
    LLVMObjCopy
    LLVMMCA
    LLVMMCDisassembler
    LLVMLTO
    LLVMFrontendOpenACC
    LLVMFrontendHLSL
    LLVMFrontendDriver
    LLVMExtensions
    LLVMPasses
    LLVMHipStdPar
    LLVMCoroutines
    LLVMCFGuard
    LLVMipo
    LLVMInstrumentation
    LLVMVectorize
    LLVMLinker
    LLVMFrontendOpenMP
    LLVMFrontendOffloading
    LLVMDWARFLinkerParallel
    LLVMDWARFLinkerClassic
    LLVMDWARFLinker
    LLVMGlobalISel
    LLVMMIRParser
    LLVMAsmPrinter
    LLVMSelectionDAG
    LLVMCodeGen
    LLVMTarget
    LLVMObjCARCOpts
    LLVMCodeGenTypes
    LLVMIRPrinter
    LLVMInterfaceStub
    LLVMFileCheck
    LLVMFuzzMutate
    LLVMScalarOpts
    LLVMInstCombine
    LLVMAggressiveInstCombine
    LLVMTransformUtils
    LLVMBitWriter
    LLVMAnalysis
    LLVMProfileData
    LLVMSymbolize
    LLVMDebugInfoBTF
    LLVMDebugInfoPDB
    LLVMDebugInfoMSF
    LLVMDebugInfoDWARF
    LLVMObject
    LLVMTextAPI
    LLVMMCParser
    LLVMIRReader
    LLVMAsmParser
    LLVMMC
    LLVMDebugInfoCodeView
    LLVMBitReader
    LLVMFuzzerCLI
    LLVMCore
    LLVMRemarks
    LLVMBitstreamReader
    LLVMBinaryFormat
    LLVMTargetParser
    LLVMTableGen
    LLVMSupport
    LLVMDemangle
)

target_compile_options(cmajor_compiler PRIVATE -O2 -fexceptions)

target_link_options(cmajor_compiler
    PRIVATE
    "SHELL:-s NO_DISABLE_EXCEPTION_CATCHING"
    "SHELL:-s MODULARIZE=1"
    "SHELL:-s EXPORT_ES6=1"
    "SHELL:-s EXPORTED_RUNTIME_METHODS=allocate,ALLOC_NORMAL,UTF8ToString,intArrayFromString"
    "SHELL:-s EXPORTED_FUNCTIONS=['_malloc','_free']"
    "SHELL:-s ALLOW_MEMORY_GROWTH=1"
    "SHELL:-s MAXIMUM_MEMORY=4gb"
    "SHELL:-s STACK_SIZE=10mb"
)

target_include_directories(cmajor_compiler
    PRIVATE
        ../../3rdParty
        ../../3rdParty/llvm/release/wasm/wasm32/include
        ../../include
)

target_link_libraries (cmajor_compiler
    PRIVATE
        ${EXTRA_LIBS}
)

target_link_directories (cmajor_compiler
    PUBLIC
        ../../cmajor/3rdParty/llvm/release/wasm/wasm32/lib
)
