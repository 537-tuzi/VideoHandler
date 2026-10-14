# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "")
  file(REMOVE_RECURSE
  "CMakeFiles/video_handler_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/video_handler_autogen.dir/ParseCache.txt"
  "video_handler_autogen"
  )
endif()
