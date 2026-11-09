# Every warning in Serialite's own components fails the build, unused code
# included (ESP-IDF only warns about it). Third-party components keep ESP-IDF's
# defaults. Call after project().
#
#   serialite_strict_warnings(<component>... <dir>...)
#
# A component directory stands for that component; any other directory for
# every component directory inside it.
function(serialite_strict_warnings)
    set(names)
    foreach(arg ${ARGN})
        if(EXISTS "${arg}/CMakeLists.txt")
            get_filename_component(name "${arg}" NAME)
            list(APPEND names ${name})
        elseif(IS_DIRECTORY "${arg}")
            file(GLOB children LIST_DIRECTORIES true "${arg}/*")
            foreach(child ${children})
                if(EXISTS "${child}/CMakeLists.txt")
                    get_filename_component(name "${child}" NAME)
                    list(APPEND names ${name})
                endif()
            endforeach()
        else()
            list(APPEND names ${arg})
        endif()
    endforeach()

    foreach(name ${names})
        idf_component_get_property(lib ${name} COMPONENT_LIB)
        get_target_property(type ${lib} TYPE)
        if(NOT type STREQUAL "INTERFACE_LIBRARY")
            target_compile_options(${lib} PRIVATE
                -Werror
                -Werror=unused-function
                -Werror=unused-variable
                -Werror=unused-but-set-variable)
        endif()
    endforeach()
endfunction()
