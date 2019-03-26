-- name of the entire codebase
workspace "classicube"
  -- what ways this project can be built (dbg is the default because it is first)
  configurations { "dbg", "dist" }
  -- written in C (this only matters in VS)
  language "C"

  toolset "gcc"

  flags { "fatalwarnings", "linktimeoptimization" }

  filter { "action:gmake*", "toolset:gcc" }
    buildoptions {
      "-Wall", "-std=c11", "-Wextra", "-Wfloat-equal", "-Winline", "-Wundef", "-Werror",
      "-fverbose-asm", "-Wint-to-pointer-cast", "-Wshadow", "-Wpointer-arith",
      "-Wcast-align", "-Wcast-qual", "-Wunreachable-code", "-Wstrict-overflow=5",
      "-Wwrite-strings", "-Wconversion", "--pedantic-errors",
      "-Wredundant-decls", "-Werror=maybe-uninitialized",
      "-Wmissing-declarations", "-Wmissing-parameter-type",
      "-Wmissing-prototypes", "-Wnested-externs", "-Wold-style-declaration",
      "-Wold-style-definition", "-Wstrict-prototypes", "-Wpointer-sign",
      "-Wno-cast-qual", "-Wno-cast-function-type",
    }

  filter "configurations:dbg"
    optimize "Off"
    symbols "On"
    buildoptions { "-ggdb3", "-O0" }

  filter "configurations:dist"
    optimize "Full"
    symbols "Off"
    buildoptions { "-O3", "-fomit-frame-pointer" }

  filter {}

  -- make an executable
  project "Classicube"
    kind "consoleapp"

    files { "src/*.c" }

    links { "m", "X11", "pthread", "GL", "curl", "openal", "dl" }

    targetdir "bin/%{cfg.buildcfg}"

  project "clobber"
    kind "makefile"

    -- on not windows, clean like this
    filter "system:not windows"
      cleancommands {
        "({RMDIR} bin obj *.make Makefile *.o -r 2>/dev/null; echo)"
      }

    -- otherwise, clean like this
    filter "system:windows"
      cleancommands {
        "{DELETE} *.make Makefile *.o",
        "{RMDIR} bin obj"
      }
