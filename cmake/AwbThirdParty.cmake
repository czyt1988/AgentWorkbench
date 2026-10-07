# Third-party integration helpers that must run at configure time.

# Rewrite qmsetup's generated forwarding headers to use absolute paths.
#
# Why: SARibbon's nested qwindowkit dependency generates "indirect reference"
# headers (qm_sync_include -> qmcorecmd incsync) in the build tree, one line
# of the form
#     #include "../../../../<source-tree>/some/header.h"
# whose relative path is anchored at the *build* directory. MSVC resolves the
# quoted include against the forwarding header's own directory, so the full
# path the compiler opens is
#     <build>/third_party/SARibbon/3rdparty/qwindowkit/include/QWKCore/private
#         + ../../../../../../../../<back to C:/src/Qt>/... .
# With an in-tree build directory ("build/") that stays well under MAX_PATH,
# but Qt Creator's default shadow build directory
# (<project-parent>/build/Desktop_<kit>-<config>, two levels deeper and
# sharing the path with the source tree) pushes the concatenated path past
# 260 characters. cl.exe is not long-path aware and reports C1083 for a file
# that provably exists ("No such file or directory" for a >MAX_PATH path).
#
# The fix: after add_subdirectory(third_party/SARibbon) has run incsync,
# rewrite every one-line relative #include in the generated include tree to
# the normalized absolute source path. Shorter than the concatenation (the
# ../../ chain re-descends the whole qwindowkit path), so it fixes deep
# shadow builds without disturbing in-tree ones.
#
# Rewrite vs. flag: the files are written by an external tool at configure
# time (FORCE makes incsync recreate them on every run) and no build rule
# owns them, so rewriting right after generation is safe and idempotent.
# Alternative escape hatches (LongPathsEnabled registry + a long-path-aware
# cl.exe manifest, or /Zc:preprocessor tricks) change machine-global state
# or don't affect quoted-include resolution at all.
#
# awb_absolutize_forwarding_headers(<include-root>)
#   <include-root>  generated include tree to rewrite (recursively)
function(awb_absolutize_forwarding_headers _root)
    if(NOT IS_DIRECTORY "${_root}")
        return()
    endif()
    file(GLOB_RECURSE _headers
        LIST_DIRECTORIES false
        RELATIVE "${_root}"
        "${_root}/*.h" "${_root}/*.hpp")
    foreach(_rel IN LISTS _headers)
        set(_file "${_root}/${_rel}")
        file(READ "${_file}" _content)
        # 单行转发头：#include "任意个 ../ 开头的相对路径"
        string(REGEX MATCH "^#include[ ]+\"((\\.\\./)+[^\"]+)\"" _m "${_content}")
        if("${CMAKE_MATCH_1}" STREQUAL "")
            continue()
        endif()
        get_filename_component(_dir "${_file}" DIRECTORY)
        get_filename_component(_abs "${_dir}/${CMAKE_MATCH_1}" ABSOLUTE)
        file(WRITE "${_file}" "#include \"${_abs}\"")
    endforeach()
endfunction()
