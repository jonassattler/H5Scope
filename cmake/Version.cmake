# SPDX-FileCopyrightText: 2026 Jonas Sattler
# SPDX-License-Identifier: GPL-3.0-only

# The version number, counted out of the release tags rather than typed.
#
# Major and minor are decisions and are written here. The patch number is not a
# decision -- it is "how many releases have been cut in this series" -- so it is
# counted, from the tags that are what a release *is*.
#
# It used to count commits, from an offset. That was right while a release was a
# thing done by hand, and wrong the moment releases became tags: it made the
# version move under every commit, so the tag to push was a moving target and
# the number said how much work had happened rather than what had been
# published. Two commits during one afternoon's work moved the next release from
# v0.1.27 to v0.1.28 without anything being released at all.
#
# Now nothing but a tag moves it. Between releases the number stands still, and
# what it stands at is the version the *next* release will carry -- a build
# after v0.2.0 calls itself 0.2.1-dev, because that is what it is: the thing
# 0.2.1 will be made of. A build standing exactly on a tag calls itself that tag
# and nothing else.
#
# Only tags in this major.minor series are counted, so bumping the minor starts
# the patch at zero again without any base to remember. That is also how the
# 0.1 series was left behind: 0.1.18 was published under the counting-commits
# scheme, and counting tags from zero inside 0.1 would have gone backwards.
#
# Three ways this can be asked for a version and get nothing to count: a source
# tarball with no .git, a shallow clone, and a clone whose tags were not
# fetched. All three are reported rather than papered over -- a build that
# quietly calls itself 0.2.0 when it is something else is worse than one that
# says it does not know. The release bundle is the one exception, and it carries
# the answer with it; see BundleVersion.cmake below.

set(H5SCOPE_VERSION_MAJOR 0)
set(H5SCOPE_VERSION_MINOR 3)

function(h5scope_resolve_version)
  set(patch 0)
  set(commit "unknown")
  set(known FALSE)
  set(released FALSE)

  set(series "v${H5SCOPE_VERSION_MAJOR}.${H5SCOPE_VERSION_MINOR}.*")

  find_package(Git QUIET)
  if(GIT_FOUND AND IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      OUTPUT_VARIABLE hash
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET)

    execute_process(
      COMMAND "${GIT_EXECUTABLE}" rev-parse --is-shallow-repository
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      OUTPUT_VARIABLE shallow
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET)

    # Every release in this series that this commit descends from, and every
    # one standing on this commit itself. The count wanted is the first less
    # the second: on the commit tagged v0.2.3 there are four such tags
    # reachable and one of them is this, which is release number three.
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" tag --list "${series}" --merged HEAD
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      OUTPUT_VARIABLE reachable
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
      RESULT_VARIABLE listed)

    execute_process(
      COMMAND "${GIT_EXECUTABLE}" tag --list "${series}" --points-at HEAD
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      OUTPUT_VARIABLE here
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET)

    if(listed EQUAL 0)
      if(shallow STREQUAL "true")
        # A shallow clone has been given some commits rather than all of them,
        # and --merged answers about what it was given. CI fetches the whole
        # history for this to mean anything; see fetch-depth in
        # .github/workflows/ci.yml.
        message(WARNING
          "H5Scope: shallow clone -- the release tags reachable from here "
          "are not the ones there are, so the version would be wrong. "
          "Reporting it as unknown. Fetch the full history (fetch-depth: 0).")
      else()
        set(known TRUE)

        set(reachable_count 0)
        if(NOT reachable STREQUAL "")
          string(REPLACE "\n" ";" reachable_list "${reachable}")
          list(LENGTH reachable_list reachable_count)
        endif()

        set(here_count 0)
        if(NOT here STREQUAL "")
          string(REPLACE "\n" ";" here_list "${here}")
          list(LENGTH here_list here_count)
          set(released TRUE)
        endif()

        math(EXPR patch "${reachable_count} - ${here_count}")
      endif()
      set(commit "${hash}")
    endif()

    # A new commit no longer changes the version, but a new *tag* does, and so
    # does moving onto or off one. HEAD covers the checkout; the packed and
    # loose tag stores cover a tag arriving or being deleted.
    foreach(witness "${CMAKE_CURRENT_SOURCE_DIR}/.git/HEAD"
                    "${CMAKE_CURRENT_SOURCE_DIR}/.git/logs/HEAD"
                    "${CMAKE_CURRENT_SOURCE_DIR}/.git/packed-refs"
                    "${CMAKE_CURRENT_SOURCE_DIR}/.git/refs/tags")
      if(EXISTS "${witness}")
        set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                     APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${witness}")
      endif()
    endforeach()
  elseif(EXISTS "${CMAKE_CURRENT_LIST_DIR}/BundleVersion.cmake")
    # The one tree with no history that still knows what it is: the source
    # bundle attached to a release. It is cut from a tagged commit by
    # tools/make-source-bundle.sh, which writes that release's numbers here
    # rather than leaving a rebuild of a released binary to call itself
    # unversioned. The file exists only inside a bundle; a clone never has one.
    include("${CMAKE_CURRENT_LIST_DIR}/BundleVersion.cmake")
    set(known TRUE)
    set(released "${H5SCOPE_BUNDLE_RELEASED}")
    set(patch "${H5SCOPE_BUNDLE_PATCH}")
    set(commit "${H5SCOPE_BUNDLE_COMMIT}")
    message(STATUS "H5Scope: versioned from the release source bundle.")
  else()
    message(WARNING
      "H5Scope: no git history here, so the releases cannot be counted. "
      "Reporting the version as unknown.")
  endif()

  set(version "${H5SCOPE_VERSION_MAJOR}.${H5SCOPE_VERSION_MINOR}.${patch}")
  set(H5SCOPE_VERSION "${version}" PARENT_SCOPE)
  set(H5SCOPE_VERSION_PATCH "${patch}" PARENT_SCOPE)
  set(H5SCOPE_COMMIT "${commit}" PARENT_SCOPE)
  set(H5SCOPE_RELEASED "${released}" PARENT_SCOPE)

  # What the About dialog prints. Three states, and each says a different thing
  # to someone trying to work out what they are running: this is release 0.2.1;
  # this is on the way to 0.2.1 and is not it; this build cannot tell.
  if(NOT known)
    set(H5SCOPE_VERSION_STRING "${version} (unversioned build)" PARENT_SCOPE)
  elseif(released)
    set(H5SCOPE_VERSION_STRING "${version}" PARENT_SCOPE)
  else()
    set(H5SCOPE_VERSION_STRING "${version}-dev" PARENT_SCOPE)
  endif()
endfunction()
