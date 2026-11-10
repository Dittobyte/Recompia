# SPDX-License-Identifier: GPL-3.0-or-later
# Linux twins: a source with a Linux implementation of its own (<name>_linux.cpp next to <name>.cpp) is built only on Windows, and the
# twin only on Linux. nxrt_pick_platform_twins(<list variable>) applies that to a list of sources in place.
function(nxrt_pick_platform_twins var)
    set(srcs ${${var}})
    if(WIN32)
        list(FILTER srcs EXCLUDE REGEX "_linux\\.cpp$")
    else()
        foreach(src IN LISTS srcs)
            if(src MATCHES "_linux\\.cpp$")
                string(REGEX REPLACE "_linux\\.cpp$" ".cpp" twin "${src}")
                list(REMOVE_ITEM srcs "${twin}")
            endif()
        endforeach()
    endif()
    set(${var} ${srcs} PARENT_SCOPE)
endfunction()
