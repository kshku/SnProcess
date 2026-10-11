set(HEADERFILES
    api.h
    command.h
    pipe.h
    process.h
    snprocess.h
)

set(SRCS
    command.c
    snprocess.c
)

set(SPECIFIC_SRCS
    pipe.c
    process.c
)

set(SUBDIRS
    nix
    win32
)

set(INCLUDE_BASE "${PROJECT_SOURCE_DIR}/process/include/snprocess")

list(TRANSFORM HEADERFILES PREPEND "${INCLUDE_BASE}/")

target_sources(snprocess PRIVATE ${HEADERFILES} ${SRCS})

foreach(dir IN LISTS SUBDIRS)
    foreach(src IN LISTS SPECIFIC_SRCS)
        target_sources(snprocess PRIVATE "${dir}/${src}")
    endforeach()
endforeach()
