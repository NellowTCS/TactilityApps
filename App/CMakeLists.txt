cmake_minimum_required(VERSION 3.20)

if (DEFINED ENV{ESP_IDF_VERSION})
    # Read board id/project
    include("../Buildscripts/board.cmake")
    init_tactility_globals("../sdkconfig")
    get_property(TACTILITY_BOARD_PROJECT GLOBAL PROPERTY TACTILITY_BOARD_PROJECT)

    idf_component_register(
        SRC_DIRS "Source"
        "Source/HelloWorld"
        "Source/TouchCalibrationApp"
        INCLUDE_DIRS "../TactilityCore/Include/Tactility"
        "../Libraries/lvgl/src"
        "../Libraries/lvgl/src/core"
        "../Libraries/lvgl/src/misc"
        REQUIRES ${BOARD_COMPONENTS}
        REQUIRES Tactility TactilityC ${TACTILITY_BOARD_PROJECT}
    )
else ()

    file(GLOB_RECURSE SOURCES "Source/*.c*")
    add_executable(AppSim ${SOURCES})
    target_link_libraries(AppSim
        PRIVATE Tactility
        PRIVATE TactilityCore
        PRIVATE TactilityHeadless
        PRIVATE Simulator
        PRIVATE SDL2::SDL2-static SDL2-static
    )

    add_definitions(-D_Nullable=)
    add_definitions(-D_Nonnull=)
endif ()
