# SDK 5.3 exports check their public targets before loading dependencies.
# Import those dependencies first, in dependency order. Some bundled SDK
# dependencies install target exports without a corresponding Config file.
if(STEL_LINPHONE_ROOT)
  foreach(export XML2/cmake/XML2Targets.cmake SQLITE3/cmake/Sqlite3Targets.cmake
                 libjpeg-turbo/cmake/TurboJpegTargets.cmake YUV/cmake/YUVTargets.cmake)
    if(EXISTS "${STEL_LINPHONE_ROOT}/share/${export}")
      include("${STEL_LINPHONE_ROOT}/share/${export}")
    endif()
  endforeach()
endif()
find_package(BCToolbox CONFIG REQUIRED)
find_package(Ortp CONFIG REQUIRED)
find_package(Belr CONFIG REQUIRED)
find_package(BelleSIP CONFIG REQUIRED)
find_package(Mediastreamer2 CONFIG REQUIRED)
find_package(LibLinphone CONFIG REQUIRED)
