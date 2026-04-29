cmake_minimum_required(VERSION 3.22)

project(Pentagon VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(PENTAGON_USE_FETCHCONTENT "Download JUCE automatically when JUCE_DIR or third_party/JUCE are unavailable." ON)
option(PENTAGON_BUILD_TESTS "Build Pentagon smoke tests." ON)

if (DEFINED JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
elseif (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE/CMakeLists.txt")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE" JUCE)
elseif (PENTAGON_USE_FETCHCONTENT)
    include(FetchContent)
    FetchContent_Declare(
        juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.7
    )
    FetchContent_MakeAvailable(juce)
else()
    message(FATAL_ERROR "JUCE not found. Set JUCE_DIR, add third_party/JUCE, or enable PENTAGON_USE_FETCHCONTENT.")
endif()

juce_add_plugin(Pentagon
    COMPANY_NAME "Pentagon Audio"
    BUNDLE_ID com.pentagon.audio
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Pgon
    PLUGIN_CODE Pntg
    FORMATS VST3
    PRODUCT_NAME "Pentagon"
)

target_sources(Pentagon
    PRIVATE
        Source/ParameterIds.h
        Source/StageProcessors.h
        Source/DSP/StageCommon.h
        Source/DSP/Fet76Stage.h
        Source/DSP/Opto2AStage.h
        Source/DSP/Vca160Stage.h
        Source/DSP/VariMuStage.h
        Source/DSP/Tube670Stage.h
        Source/DSP/StageChainManager.h
        Source/PluginProcessor.h
        Source/PluginProcessor.cpp
        Source/PluginEditor.h
        Source/PluginEditor.cpp
)

target_compile_definitions(Pentagon
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Pentagon
    PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

if (PENTAGON_BUILD_TESTS)
    include(CTest)

    juce_add_console_app(PentagonSmokeTests
        PRODUCT_NAME "PentagonSmokeTests"
    )

    target_sources(PentagonSmokeTests
        PRIVATE
            Tests/PentagonSmokeTests.cpp
            Source/ParameterIds.h
            Source/StageProcessors.h
            Source/DSP/StageCommon.h
            Source/DSP/Fet76Stage.h
            Source/DSP/Opto2AStage.h
            Source/DSP/Vca160Stage.h
            Source/DSP/VariMuStage.h
            Source/DSP/Tube670Stage.h
            Source/DSP/StageChainManager.h
            Source/PluginProcessor.h
            Source/PluginProcessor.cpp
            Source/PluginEditor.h
            Source/PluginEditor.cpp
    )

    target_compile_definitions(PentagonSmokeTests
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_VST3_CAN_REPLACE_VST2=0
    )

    target_link_libraries(PentagonSmokeTests
        PRIVATE
            juce::juce_audio_processors
            juce::juce_audio_utils
            juce::juce_dsp
            juce::juce_gui_extra
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags
    )

    add_test(NAME PentagonSmokeTests COMMAND PentagonSmokeTests)

    juce_add_console_app(PentagonUIScreenshot
        PRODUCT_NAME "PentagonUIScreenshot"
    )

    target_sources(PentagonUIScreenshot
        PRIVATE
            Tests/PentagonUIScreenshot.cpp
            Source/ParameterIds.h
            Source/StageProcessors.h
            Source/DSP/StageCommon.h
            Source/DSP/Fet76Stage.h
            Source/DSP/Opto2AStage.h
            Source/DSP/Vca160Stage.h
            Source/DSP/VariMuStage.h
            Source/DSP/Tube670Stage.h
            Source/DSP/StageChainManager.h
            Source/PluginProcessor.h
            Source/PluginProcessor.cpp
            Source/PluginEditor.h
            Source/PluginEditor.cpp
    )

    target_compile_definitions(PentagonUIScreenshot
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_VST3_CAN_REPLACE_VST2=0
    )

    target_link_libraries(PentagonUIScreenshot
        PRIVATE
            juce::juce_audio_processors
            juce::juce_audio_utils
            juce::juce_dsp
            juce::juce_gui_extra
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags
    )
endif()
