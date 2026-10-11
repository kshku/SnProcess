set(HEADERFILES
    api.h
    snprocess.h
)

set(SRCS
    snprocess.c
)

set(INCLUDE_BASE "${PROJECT_SOURCE_DIR}/process/include/snprocess")

list(TRANSFORM HEADERFILES PREPEND "${INCLUDE_BASE}/")

target_sources(snprocess PRIVATE ${HEADERFILES} ${SRCS})
