cmake_minimum_required(VERSION 3.22)
# Operating-system headers belong in src/platform/; the rest of the program uses its interface, so
# a port to another system replaces only that directory. Standard C and C++ headers such as
# <csignal>, and library headers such as CommonCrypto's, are allowed everywhere.
set(system_headers
  "unistd|fcntl|dlfcn|sched|signal|pthread|spawn|dirent|poll|pwd|grp|termios"
  "windows|winbase|io|direct|process|shlobj|aclapi|winioctl"
  "sys/[^>]+|mach/[^>]+|mach-o/[^>]+")
string(JOIN "|" system_headers ${system_headers})
set(pattern "^[ \t]*#[ \t]*include[ \t]*<(${system_headers})\\.h>")

get_filename_component(WT_SOURCE "${WT_SOURCE}" ABSOLUTE)
file(GLOB_RECURSE sources RELATIVE "${WT_SOURCE}/src" "${WT_SOURCE}/src/*.cpp" "${WT_SOURCE}/src/*.hpp")
list(FILTER sources EXCLUDE REGEX "^platform/")
list(LENGTH sources count)
if(count LESS 10)
  message(FATAL_ERROR "Expected project sources under ${WT_SOURCE}/src, found ${count}")
endif()
set(violations)
foreach(source IN LISTS sources)
  file(STRINGS "${WT_SOURCE}/src/${source}" includes REGEX "${pattern}")
  foreach(line IN LISTS includes)
    string(STRIP "${line}" line)
    list(APPEND violations "src/${source}: ${line}")
  endforeach()
endforeach()
if(violations)
  list(JOIN violations "\n  " violations)
  message(FATAL_ERROR "Operating-system headers outside src/platform/:\n  ${violations}")
endif()
