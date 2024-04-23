cmake_minimum_required (VERSION 3.16..3.22)

set_property (GLOBAL PROPERTY CMAJ_MODULE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

macro (TRANSFORM_BOOL VARIABLE)
    if (${VARIABLE})
        set (${VARIABLE} 1)
    else()
        set (${VARIABLE} 0)
    endif()
endmacro()

function (MAKE_CMAJ_LIBRARY)

    get_property (CMAJ_MODULE_DIR GLOBAL PROPERTY CMAJ_MODULE_DIR)

    # -------------------------------------------------------------------
    # Parse arguments to determine options

    set (options
        INCLUDE_SERVER
        INCLUDE_SCRIPTING
        INCLUDE_PLAYBACK

        ENABLE_PERFORMER_LLVM
        ENABLE_PERFORMER_WEBVIEW
        ENABLE_PERFORMER_CPP

        ENABLE_CODEGEN_CPP
        ENABLE_CODEGEN_LLVM_WASM
    )

    set (oneValueArgs LIBRARY_NAME)

    cmake_parse_arguments (CMAJ "${options}" "${oneValueArgs}" "" ${ARGN})

    message ("Configuring Cmajor lib:           ${CMAJ_LIBRARY_NAME}")
    message ("CMAJ_INCLUDE_SERVER:              ${CMAJ_INCLUDE_SERVER}")
    message ("CMAJ_INCLUDE_SCRIPTING:           ${CMAJ_INCLUDE_SCRIPTING}")
    message ("CMAJ_INCLUDE_PLAYBACK:            ${CMAJ_INCLUDE_PLAYBACK}")
    message ("CMAJ_ENABLE_PERFORMER_LLVM:       ${CMAJ_ENABLE_PERFORMER_LLVM}")
    message ("CMAJ_ENABLE_PERFORMER_WEBVIEW:    ${CMAJ_ENABLE_PERFORMER_WEBVIEW}")
    message ("CMAJ_ENABLE_PERFORMER_CPP:        ${CMAJ_ENABLE_PERFORMER_CPP}")
    message ("CMAJ_ENABLE_CODEGEN_CPP:          ${CMAJ_ENABLE_CODEGEN_CPP}")
    message ("CMAJ_ENABLE_CODEGEN_LLVM_WASM:    ${CMAJ_ENABLE_CODEGEN_LLVM_WASM}")

    # -------------------------------------------------------------------
    # LLVM Stuff

    set (LLVM_RELEASE_DIR ${CMAJ_MODULE_DIR}/../3rdParty/llvm/release)

    if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        set (LLVM_PATH_ROOT ${LLVM_RELEASE_DIR}/osx/universal)
        set (LLVM_EXTRA_LIBS z)
    elseif (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
        if (${CMAKE_SYSTEM_PROCESSOR} MATCHES "aarch64")
            set (LLVM_PATH_ROOT ${LLVM_RELEASE_DIR}/linux/arm64)
        elseif (${CMAKE_SYSTEM_PROCESSOR} MATCHES "armv7l")
            set (LLVM_PATH_ROOT ${LLVM_RELEASE_DIR}/linux/arm32)
        else()
            set (LLVM_PATH_ROOT ${LLVM_RELEASE_DIR}/linux/x64)
        endif()
        set (LLVM_EXTRA_LIBS z dl pthread)
    elseif (${CMAKE_SYSTEM_NAME} MATCHES "Android")
        if (${CMAKE_SYSTEM_PROCESSOR} MATCHES "aarch64")
            set (LLVM_PATH_ROOT ${LLVM_RELEASE_DIR}/android/arm64)
        endif()
        set (LLVM_EXTRA_LIBS z)
    elseif (${CMAKE_SYSTEM_NAME} MATCHES "Windows")
        if (${CMAKE_GENERATOR_PLATFORM} MATCHES "arm64")
            set (LLVM_PATH_ROOT ${LLVM_RELEASE_DIR}/win/arm64)
        else()
            set (LLVM_PATH_ROOT ${LLVM_RELEASE_DIR}/win/x64)
        endif()
    endif()

    # Determine which LLVM platforms are supported by this LLVM build
    include (${LLVM_PATH_ROOT}/cmake_platforms)

    if (LLVM_INCLUDE_PLATFORM_WASM)
        message ("Including wasm")
        set (LLVM_PLATFORM_LIBS
            ${LLVM_PLATFORM_LIBS}
            LLVMWebAssemblyDisassembler
            LLVMWebAssemblyAsmParser
            LLVMWebAssemblyCodeGen
            LLVMWebAssemblyUtils
            LLVMWebAssemblyDesc
            LLVMWebAssemblyInfo
        )
    endif()

    if (LLVM_INCLUDE_PLATFORM_ARM64)
        message ("Including arm64")
        set (LLVM_PLATFORM_LIBS
            ${LLVM_PLATFORM_LIBS}
            LLVMAArch64Disassembler
            LLVMAArch64AsmParser
            LLVMAArch64CodeGen
            LLVMAArch64Desc
            LLVMAArch64Utils
            LLVMAArch64Info
        )
    endif()

    if (LLVM_INCLUDE_PLATFORM_ARM32)
        message ("Including arm32")
        set (LLVM_PLATFORM_LIBS
            ${LLVM_PLATFORM_LIBS}
            LLVMARMDisassembler
            LLVMARMAsmParser
            LLVMARMCodeGen
            LLVMARMDesc
            LLVMARMUtils
            LLVMARMInfo
        )
    endif()

    if (LLVM_INCLUDE_PLATFORM_X64)
        message ("Including x64")
        set (LLVM_PLATFORM_LIBS
            ${LLVM_PLATFORM_LIBS}
            LLVMX86TargetMCA
            LLVMX86Disassembler
            LLVMX86AsmParser
            LLVMX86CodeGen
            LLVMX86Desc
            LLVMX86Info
        )
    endif()


    if (LLVM_INCLUDE_PLATFORM_HEXAGON)
        message ("Including hexagon")
        set (LLVM_PLATFORM_LIBS
            ${LLVM_PLATFORM_LIBS}
            LLVMHexagonDisassembler
            LLVMHexagonAsmParser
            LLVMHexagonCodeGen
            LLVMHexagonDesc
            LLVMHexagonInfo
        )
    endif()

    if (LLVM_INCLUDE_PLATFORM_RISCV)
        message ("Including riscv")
        set (LLVM_PLATFORM_LIBS
            ${LLVM_PLATFORM_LIBS}
            LLVMRISCVDisassembler
            LLVMRISCVAsmParser
            LLVMRISCVCodeGen
            LLVMRISCVDesc
            LLVMRISCVInfo
        )
    endif()


    set (LLVM_LIBS

        LLVMWindowsManifest
        LLVMWindowsDriver
        LLVMXRay
        LLVMLibDriver
        LLVMDlltoolDriver
        LLVMCoverage
        LLVMLineEditor

        ${LLVM_PLATFORM_LIBS}

        LLVMOrcJIT
        LLVMMCJIT
        LLVMJITLink
        LLVMInterpreter
        LLVMExecutionEngine
        LLVMRuntimeDyld
        LLVMOrcTargetProcess
        LLVMOrcShared
        LLVMDWP
        LLVMDebugInfoGSYM
        LLVMOption
        LLVMObjectYAML
        LLVMObjCopy
        LLVMMCA
        LLVMMCDisassembler
        LLVMLTO
        LLVMCFGuard
        LLVMFrontendOpenACC
        LLVMExtensions
        Polly
        PollyISL
        LLVMPasses
        LLVMObjCARCOpts
        LLVMCoroutines
        LLVMipo
        LLVMInstrumentation
        LLVMVectorize
        LLVMLinker
        LLVMFrontendOpenMP
        LLVMDWARFLinker
        LLVMGlobalISel
        LLVMMIRParser
        LLVMAsmPrinter
        LLVMSelectionDAG
        LLVMCodeGen
        LLVMIRReader
        LLVMAsmParser
        LLVMInterfaceStub
        LLVMFileCheck
        LLVMFuzzMutate
        LLVMTarget
        LLVMScalarOpts
        LLVMInstCombine
        LLVMAggressiveInstCombine
        LLVMTransformUtils
        LLVMBitWriter
        LLVMAnalysis
        LLVMProfileData
        LLVMSymbolize
        LLVMDebugInfoPDB
        LLVMDebugInfoMSF
        LLVMDebugInfoDWARF
        LLVMObject
        LLVMTextAPI
        LLVMMCParser
        LLVMMC
        LLVMDebugInfoCodeView
        LLVMBitReader
        LLVMFuzzerCLI
        LLVMCore
        LLVMRemarks
        LLVMBitstreamReader
        LLVMBinaryFormat
        LLVMTableGen
        LLVMSupport
        LLVMDemangle
    )

    # -------------------------------------------------------------------
    file (GLOB_RECURSE CHOC_SOURCE
        ${CMAJ_MODULE_DIR}/../include/choc/*.h
    )

    source_group (TREE "${CMAJ_MODULE_DIR}/../include/choc" PREFIX "choc" FILES ${CHOC_SOURCE})

    # -------------------------------------------------------------------
    file (GLOB_RECURSE CMAJOR_SOURCE
        ${CMAJ_MODULE_DIR}/../include/cmajor/*.h
    )

    source_group (TREE "${CMAJ_MODULE_DIR}/../include/cmajor" PREFIX "cmajor" FILES ${CMAJOR_SOURCE})

    # -------------------------------------------------------------------

    file (GLOB_RECURSE SOURCE
        ${CMAJ_MODULE_DIR}/compiler/include/*.h
        ${CMAJ_MODULE_DIR}/compiler/src/*.h
        ${CMAJ_MODULE_DIR}/compiler/src/*.cpp
        ${CMAJ_MODULE_DIR}/compiler/src/backends/WebAssembly/wasm3/*.c
    )

    if (CMAJ_INCLUDE_PLAYBACK)
        file (GLOB_RECURSE PLAYBACK_SOURCE
            ${CMAJ_MODULE_DIR}/playback/include/*.h
            ${CMAJ_MODULE_DIR}/playback/src/*.h
            ${CMAJ_MODULE_DIR}/playback/src/*.cpp
        )

        set (SOURCE ${SOURCE} ${PLAYBACK_SOURCE})
    endif()

    if (CMAJ_INCLUDE_SERVER)
        file (GLOB_RECURSE SERVER_SOURCE
            ${CMAJ_MODULE_DIR}/server/include/*.h
            ${CMAJ_MODULE_DIR}/server/src/*.h
            ${CMAJ_MODULE_DIR}/server/src/*.cpp
        )

        set (SOURCE ${SOURCE} ${SERVER_SOURCE})
    endif()

    if (CMAJ_INCLUDE_SCRIPTING)
        file (GLOB_RECURSE SCRIPTING_SOURCE
            ${CMAJ_MODULE_DIR}/scripting/include/*.h
            ${CMAJ_MODULE_DIR}/scripting/src/*.h
            ${CMAJ_MODULE_DIR}/scripting/src/*.cpp
        )

        set (SOURCE ${SOURCE} ${SCRIPTING_SOURCE})
    endif()

    source_group (TREE "${CMAJ_MODULE_DIR}" PREFIX "modules" FILES ${SOURCE})

    TRANSFORM_BOOL (CMAJ_ENABLE_PERFORMER_LLVM)
    TRANSFORM_BOOL (CMAJ_ENABLE_PERFORMER_WEBVIEW)
    TRANSFORM_BOOL (CMAJ_ENABLE_PERFORMER_CPP)
    TRANSFORM_BOOL (CMAJ_ENABLE_CODEGEN_CPP)
    TRANSFORM_BOOL (CMAJ_ENABLE_CODEGEN_LLVM_WASM)

    add_compile_definitions (
        $<$<CONFIG:Debug>:DEBUG=1>
        CMAJOR_DLL=0
        $<$<CONFIG:Debug>:CMAJ_ENABLE_ALLOCATION_CHECKER=1>
        CMAJ_ENABLE_PERFORMER_LLVM=${CMAJ_ENABLE_PERFORMER_LLVM}
        CMAJ_ENABLE_PERFORMER_WEBVIEW=${CMAJ_ENABLE_PERFORMER_WEBVIEW}
        CMAJ_ENABLE_PERFORMER_CPP=${CMAJ_ENABLE_PERFORMER_CPP}
        CMAJ_ENABLE_CODEGEN_CPP=${CMAJ_ENABLE_CODEGEN_CPP}
        CMAJ_ENABLE_CODEGEN_LLVM_WASM=${CMAJ_ENABLE_CODEGEN_LLVM_WASM}
    )

    add_library (${CMAJ_LIBRARY_NAME} OBJECT
        ${SOURCE}
        ${CHOC_SOURCE}
        ${CMAJOR_SOURCE}
    )

    if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        foreach(LIB ${LLVM_LIBS} ${LLVM_EXTRA_LIBS})
            target_link_options(${CMAJ_LIBRARY_NAME} INTERFACE "LINKER:-hidden-l${LIB}")
        endforeach()
    elseif (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
        target_link_options(${CMAJ_LIBRARY_NAME} INTERFACE "-Wl,--exclude-libs,ALL")
    endif()

    target_sources(${CMAJ_LIBRARY_NAME} PRIVATE ${CMAJ_MODULE_DIR}/../3rdParty/graphviz/cmaj_GraphViz.cpp)

    target_compile_features (${CMAJ_LIBRARY_NAME} PRIVATE cxx_std_17)

    target_compile_options (${CMAJ_LIBRARY_NAME} PRIVATE ${CMAJ_WARNING_FLAGS})

    target_compile_definitions (${CMAJ_LIBRARY_NAME}
        PRIVATE
            CMAJ_VERSION="${CMAJ_VERSION}"
            CHOC_ASSERT=must_include_assertion_header_before_any_choc_headers
    )

    target_include_directories (${CMAJ_LIBRARY_NAME}
        PUBLIC
            ${CMAJ_MODULE_DIR}/../include
    )

    target_include_directories (${CMAJ_LIBRARY_NAME}
        PRIVATE
            ${CMAJ_MODULE_DIR}/../3rdParty
            ${LLVM_PATH_ROOT}/include
    )

    target_include_directories(${CMAJ_LIBRARY_NAME}
        SYSTEM PRIVATE
            ${CMAJ_MODULE_DIR}/../3rdParty/boost/align/include/
            ${CMAJ_MODULE_DIR}/../3rdParty/boost/asio/include/
            ${CMAJ_MODULE_DIR}/../3rdParty/boost/assert/include/
            ${CMAJ_MODULE_DIR}/../3rdParty/boost/beast/include/
            ${CMAJ_MODULE_DIR}/../3rdParty/boost/bind/include/
            ${CMAJ_MODULE_DIR}/../3rdParty/boost/config/include/
            ${CMAJ_MODULE_DIR}/../3rdParty/boost/core/include/
            ${CMAJ_MODULE_DIR}/../3rdParty/boost/date_time/include/
            ${CMAJ_MODULE_DIR}/../3rdParty/boost/endian/include/
            ${CMAJ_MODULE_DIR}/../3rdParty/boost/intrusive/include/
            ${CMAJ_MODULE_DIR}/../3rdParty/boost/is_placeholder/include/
            ${CMAJ_MODULE_DIR}/../3rdParty/boost/io/include/
            ${CMAJ_MODULE_DIR}/../3rdParty/boost/logic/include/
            ${CMAJ_MODULE_DIR}/../3rdParty/boost/optional/include/
            ${CMAJ_MODULE_DIR}/../3rdParty/boost/static_assert/include/
            ${CMAJ_MODULE_DIR}/../3rdParty/boost/smart_ptr/include/
            ${CMAJ_MODULE_DIR}/../3rdParty/boost/system/include/
            ${CMAJ_MODULE_DIR}/../3rdParty/boost/move/include/
            ${CMAJ_MODULE_DIR}/../3rdParty/boost/mp11/include/
            ${CMAJ_MODULE_DIR}/../3rdParty/boost/mpl/include/
            ${CMAJ_MODULE_DIR}/../3rdParty/boost/numeric_conversion/include/
            ${CMAJ_MODULE_DIR}/../3rdParty/boost/preprocessor/include/
            ${CMAJ_MODULE_DIR}/../3rdParty/boost/predef/include/
            ${CMAJ_MODULE_DIR}/../3rdParty/boost/regex/include/
            ${CMAJ_MODULE_DIR}/../3rdParty/boost/throw_exception/include/
            ${CMAJ_MODULE_DIR}/../3rdParty/boost/type_traits/include/
            ${CMAJ_MODULE_DIR}/../3rdParty/boost/utility/include/
            ${CMAJ_MODULE_DIR}/../3rdParty/boost/winapi/include/
            ${CMAJ_MODULE_DIR}/../3rdParty/boost/static_string/include/
    )

    target_link_directories (${CMAJ_LIBRARY_NAME}
        PUBLIC
            ${LLVM_PATH_ROOT}/lib
    )

    target_link_libraries (${CMAJ_LIBRARY_NAME}
        PRIVATE
            ${LLVM_LIBS}
            ${LLVM_EXTRA_LIBS}
    )

    if (CMAJ_INCLUDE_SCRIPTING)
        if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
            find_package(PkgConfig REQUIRED)
            pkg_check_modules (GTK3 REQUIRED gtk+-3.0 IMPORTED_TARGET)
            pkg_check_modules (WEBKIT2 REQUIRED webkit2gtk-4.0 IMPORTED_TARGET)
            target_link_libraries (${CMAJ_LIBRARY_NAME} PUBLIC ${GTK3_LIBRARIES} ${WEBKIT2_LIBRARIES})
            target_include_directories(${CMAJ_LIBRARY_NAME} PUBLIC ${GTK3_INCLUDE_DIRS} ${WEBKIT2_INCLUDE_DIRS})
        endif()
    endif()

    if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
        if (${CMAKE_SYSTEM_PROCESSOR} MATCHES "armv7l")
            target_link_libraries (${CMAJ_LIBRARY_NAME} PUBLIC atomic)
        endif()
    endif()


endfunction()