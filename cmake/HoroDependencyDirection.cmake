include_guard(GLOBAL)

function(_horo_dependency_key output value)
    string(SHA256 key "${value}")
    set(${output} "${key}" PARENT_SCOPE)
endfunction()

function(_horo_canonical_target output target)
    if(TARGET "${target}")
        get_target_property(aliased_target "${target}" ALIASED_TARGET)
        if(aliased_target AND NOT aliased_target STREQUAL "aliased_target-NOTFOUND")
            set(${output} "${aliased_target}" PARENT_SCOPE)
            return()
        endif()
    endif()

    set(${output} "${target}" PARENT_SCOPE)
endfunction()

function(horo_allow_target_dependencies)
    cmake_parse_arguments(ARG "" "TARGET" "DEPENDENCIES" ${ARGN})
    if(ARG_UNPARSED_ARGUMENTS OR NOT ARG_TARGET)
        message(FATAL_ERROR
            "horo_allow_target_dependencies requires TARGET and optional DEPENDENCIES arguments")
    endif()

    _horo_dependency_key(target_key "${ARG_TARGET}")
    get_property(existing GLOBAL PROPERTY "HORO_DEPENDENCY_POLICY_${target_key}" SET)
    if(existing)
        message(FATAL_ERROR "Dependency policy for '${ARG_TARGET}' is declared more than once")
    endif()

    set_property(GLOBAL PROPERTY "HORO_DEPENDENCY_POLICY_${target_key}" "${ARG_DEPENDENCIES}")
    set_property(GLOBAL APPEND PROPERTY HORO_DEPENDENCY_POLICY_TARGETS "${ARG_TARGET}")
endfunction()

function(horo_allow_temporary_dependency_exception)
    cmake_parse_arguments(ARG "" "TARGET;DEPENDENCY;OWNER;REMOVAL_TICKET;REASON" "" ${ARGN})
    if(ARG_UNPARSED_ARGUMENTS OR NOT ARG_TARGET OR NOT ARG_DEPENDENCY OR NOT ARG_OWNER
            OR NOT ARG_REMOVAL_TICKET OR NOT ARG_REASON)
        message(FATAL_ERROR
            "Temporary dependency exceptions require TARGET, DEPENDENCY, OWNER, REMOVAL_TICKET, and REASON")
    endif()
    if(NOT ARG_REMOVAL_TICKET MATCHES "^#[1-9][0-9]*$")
        message(FATAL_ERROR
            "Temporary dependency exception '${ARG_TARGET} -> ${ARG_DEPENDENCY}' has invalid removal ticket "
            "'${ARG_REMOVAL_TICKET}'; use a GitHub issue reference such as #123")
    endif()

    _horo_dependency_key(edge_key "${ARG_TARGET}__${ARG_DEPENDENCY}")
    get_property(existing GLOBAL PROPERTY "HORO_DEPENDENCY_EXCEPTION_${edge_key}" SET)
    if(existing)
        message(FATAL_ERROR
            "Temporary dependency exception '${ARG_TARGET} -> ${ARG_DEPENDENCY}' is declared more than once")
    endif()

    set(metadata "owner=${ARG_OWNER};removal=${ARG_REMOVAL_TICKET};reason=${ARG_REASON}")
    set_property(GLOBAL PROPERTY "HORO_DEPENDENCY_EXCEPTION_${edge_key}" "${metadata}")
    set_property(GLOBAL APPEND PROPERTY HORO_DEPENDENCY_EXCEPTION_EDGES
        "${ARG_TARGET} -> ${ARG_DEPENDENCY}")
endfunction()

function(_horo_verify_policy_coverage target)
    _horo_dependency_key(target_key "${target}")
    get_property(has_policy GLOBAL PROPERTY "HORO_DEPENDENCY_POLICY_${target_key}" SET)
    if(NOT has_policy)
        message(FATAL_ERROR
            "Dependency direction policy is missing production target '${target}'. "
            "Declare its allowed direct dependencies in cmake/HoroDependencyPolicy.cmake.")
    endif()
endfunction()

function(horo_collect_directory_targets output directory)
    get_property(directory_targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(subdirectory IN LISTS subdirectories)
        horo_collect_directory_targets(subdirectory_targets "${subdirectory}")
        list(APPEND directory_targets ${subdirectory_targets})
    endforeach()
    set(${output} "${directory_targets}" PARENT_SCOPE)
endfunction()

function(_horo_verify_no_hidden_first_party_dependency target dependency policy_targets)
    if(NOT dependency MATCHES "^\\$<")
        return()
    endif()

    foreach(policy_target IN LISTS policy_targets)
        if(dependency MATCHES "(^|[^A-Za-z0-9_])${policy_target}([^A-Za-z0-9_]|$)")
            message(FATAL_ERROR
                "Dependency direction validation cannot inspect generator-expression edge "
                "'${target} -> ${dependency}'. Declare first-party dependencies directly so the policy cannot be bypassed.")
        endif()
    endforeach()

    if(dependency MATCHES "HoroEngine::" AND NOT dependency MATCHES "HoroThirdParty::")
        message(FATAL_ERROR
            "Dependency direction validation cannot inspect generator-expression edge "
            "'${target} -> ${dependency}'. Declare first-party dependencies directly so the policy cannot be bypassed.")
    endif()
endfunction()

function(_horo_unwrap_link_dependency output dependency)
    if(dependency MATCHES "^\\$<LINK_ONLY:([^<>]+)>$")
        set(${output} "${CMAKE_MATCH_1}" PARENT_SCOPE)
        return()
    endif()

    set(${output} "${dependency}" PARENT_SCOPE)
endfunction()

function(horo_verify_dependency_direction)
    cmake_parse_arguments(ARG "" "" "TARGETS" ${ARGN})
    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "Unknown dependency verification arguments: ${ARG_UNPARSED_ARGUMENTS}")
    endif()

    get_property(policy_targets GLOBAL PROPERTY HORO_DEPENDENCY_POLICY_TARGETS)
    if(NOT policy_targets)
        message(FATAL_ERROR "Dependency direction policy contains no targets")
    endif()
    list(REMOVE_DUPLICATES policy_targets)

    foreach(target IN LISTS ARG_TARGETS)
        if(TARGET "${target}")
            _horo_verify_policy_coverage("${target}")
        endif()
    endforeach()

    get_property(exception_edges GLOBAL PROPERTY HORO_DEPENDENCY_EXCEPTION_EDGES)
    set(used_exception_edges)

    foreach(target IN LISTS policy_targets)
        if(NOT TARGET "${target}")
            continue()
        endif()

        get_target_property(target_type "${target}" TYPE)
        if(target_type STREQUAL "INTERFACE_LIBRARY")
            get_target_property(dependencies "${target}" INTERFACE_LINK_LIBRARIES)
        else()
            get_target_property(dependencies "${target}" LINK_LIBRARIES)
            get_target_property(interface_dependencies "${target}" INTERFACE_LINK_LIBRARIES)
            if(interface_dependencies
                    AND NOT interface_dependencies STREQUAL "interface_dependencies-NOTFOUND")
                list(APPEND dependencies ${interface_dependencies})
            endif()
        endif()
        if(NOT dependencies OR dependencies STREQUAL "dependencies-NOTFOUND")
            continue()
        endif()

        _horo_dependency_key(target_key "${target}")
        get_property(allowed_dependencies GLOBAL PROPERTY "HORO_DEPENDENCY_POLICY_${target_key}")

        foreach(dependency IN LISTS dependencies)
            _horo_unwrap_link_dependency(link_dependency "${dependency}")
            _horo_verify_no_hidden_first_party_dependency(
                "${target}" "${link_dependency}" "${policy_targets}")
            if(NOT TARGET "${link_dependency}")
                continue()
            endif()

            _horo_canonical_target(canonical_dependency "${link_dependency}")
            if(NOT canonical_dependency IN_LIST policy_targets)
                continue()
            endif()
            if(canonical_dependency IN_LIST allowed_dependencies)
                continue()
            endif()

            _horo_dependency_key(edge_key "${target}__${canonical_dependency}")
            get_property(has_exception GLOBAL PROPERTY "HORO_DEPENDENCY_EXCEPTION_${edge_key}" SET)
            if(has_exception)
                list(APPEND used_exception_edges "${target} -> ${canonical_dependency}")
                continue()
            endif()

            if(allowed_dependencies)
                list(JOIN allowed_dependencies ", " formatted_allowed_dependencies)
            else()
                set(formatted_allowed_dependencies "<none>")
            endif()
            message(FATAL_ERROR
                "Dependency direction violation: '${target}' links to '${canonical_dependency}', but that direct edge "
                "is forbidden by cmake/HoroDependencyPolicy.cmake. Allowed first-party dependencies for '${target}': "
                "${formatted_allowed_dependencies}. Update the architecture before changing the policy; a temporary "
                "exception requires an owner, removal ticket, and reason.")
        endforeach()
    endforeach()

    foreach(exception_edge IN LISTS exception_edges)
        string(REPLACE " -> " ";" exception_targets "${exception_edge}")
        list(GET exception_targets 0 exception_target)
        list(GET exception_targets 1 exception_dependency)
        if(TARGET "${exception_target}" AND TARGET "${exception_dependency}"
                AND NOT exception_edge IN_LIST used_exception_edges)
            message(FATAL_ERROR
                "Temporary dependency exception '${exception_edge}' is stale because the edge is absent or now allowed. "
                "Remove the exception instead of keeping dormant policy debt.")
        endif()
    endforeach()

    message(STATUS "Horo dependency direction policy is satisfied")
endfunction()
