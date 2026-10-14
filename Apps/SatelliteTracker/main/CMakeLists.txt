include("$ENV{TACTILITY_SDK_PATH}/TactilitySDK.cmake")

file(GLOB_RECURSE SOURCE_FILES Source/*.c*)
tactility_component_register(SRCS ${SOURCE_FILES} REQUIRES http-module)

# The posix SDK (simulator) ignores REQUIRES and only adds a fixed list of include directories
if (NOT IDF_TARGET)
    target_include_directories(${PROJECT_NAME} PRIVATE $ENV{TACTILITY_SDK_PATH}/Modules/http-module/include)
endif ()
