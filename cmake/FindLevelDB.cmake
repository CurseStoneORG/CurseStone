# cmake/FindLevelDB.cmake — locate Google's LevelDB library (optional).
# Sets: LEVELDB_FOUND, LEVELDB_INCLUDE_DIRS, LEVELDB_LIBRARIES.

find_path(LEVELDB_INCLUDE_DIR NAMES leveldb/db.h HINTS ENV LEVELDB_ROOT PATH_SUFFIXES include)
find_library(LEVELDB_LIBRARY  NAMES leveldb       HINTS ENV LEVELDB_ROOT PATH_SUFFIXES lib lib64)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LevelDB DEFAULT_MSG LEVELDB_LIBRARY LEVELDB_INCLUDE_DIR)

if(LEVELDB_FOUND)
    set(LEVELDB_INCLUDE_DIRS ${LEVELDB_INCLUDE_DIR})
    set(LEVELDB_LIBRARIES    ${LEVELDB_LIBRARY})
    mark_as_advanced(LEVELDB_INCLUDE_DIR LEVELDB_LIBRARY)
endif()
