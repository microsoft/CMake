set(OUTPUT_NAME "test.zip")

set(ARCHIVE_FORMAT zip)
set(COMPRESSION_TYPE PPMd)

include(${CMAKE_CURRENT_LIST_DIR}/roundtrip.cmake)
