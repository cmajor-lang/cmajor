include(${CMAKE_CURRENT_LIST_DIR}/CPM.cmake)

CPMAddPackage(
    NAME            llvm
    GIT_REPOSITORY  https://github.com/cmajor-lang/llvm.git
    GIT_TAG         c6380f9
    GIT_SHALLOW     1
    GIT_PROGRESS    TRUE
    SYSTEM          YES
)
