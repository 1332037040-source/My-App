cmake_minimum_required(VERSION 3.14)
project("My App" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CORE_SOURCES
    core/Utils.cpp
    fft/FFTCore.cpp
    fft/Analyzer.cpp
    preprocess/Window.cpp
    preprocess/Preprocessing.cpp
    preprocess/AudioBlocking.cpp
    preprocess/Weighting.cpp
    preprocess/TimeWeighting.cpp
    io/WAVReader.cpp
    io/ATFXReader.cpp
    io/HDFReader.cpp
    pugixml.cpp
    domain/ParseUtils.cpp
    executor/FFTExecutor.cpp
    executor/ParallelRunner.cpp
    engine/Engine.cpp
    features/fft_vs_time/FFTvsTimeAnalyzer.cpp
    features/fft_vs_rpm/FFTvsRpmMapper.cpp
    features/octave_band/OctaveAnalyzer.cpp
    features/level_vs_time/LevelVsTimeAnalyzer.cpp
    features/level_vs_rpm/levelVsRpmAnalyzer.cpp
    features/order_tracking/AngleResampler.cpp
    app/RunRequestMapper.cpp
    planner/TaskBuilder.cpp
    services/DataReaderService.cpp
    services/spectrumService.cpp
    services/ExportService.cpp
    flows/FFTFlow.cpp
    flows/FFTvsTimeFlow.cpp
    flows/FFTvsRpmFlow.cpp
    flows/OctaveFlow.cpp
    flows/LevelVsTimeFlow.cpp
    flows/LevelVsRpmFlow.cpp
    core_api/CoreAnalysisFacade.cpp
    core_api/CoreDataFacade.cpp
)

set(CORE_HEADERS
    core_api/CoreAnalysisRequest.h
    core_api/CoreAnalysisResult.h
    core_api/CoreAnalysisFacade.h
    core_api/CoreFileInfo.h
    core_api/CoreChannelInfo.h
    core_api/CoreChannelListResult.h
    core_api/CoreSignalData.h
    core_api/CoreDataRequest.h
    core_api/CoreDataFacade.h
    features/level_vs_time/LevelVsTimeAnalyzer.h
    features/level_vs_rpm/LevelVsRpmAnalyzer.h
    flows/LevelVsTimeFlow.h
    flows/LevelVsRpmFlow.h
    flows/FFTvsRpmFlow.h
    features/order_tracking/AngleResampler.h
)

add_library(myapp_core STATIC
    ${CORE_SOURCES}
    ${CORE_HEADERS}
)

target_include_directories(myapp_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(myapp_core PRIVATE
    _CRT_SECURE_NO_WARNINGS
    _USE_MATH_DEFINES
)

if(DEFINED OpenCV_LIBS)
    target_link_libraries(myapp_core PRIVATE ${OpenCV_LIBS})
endif()

set(CLI_SOURCES
    app/main.cpp
    planner/TaskPlanner.cpp
    report/ReportWriter.cpp
)

add_executable(MyApp
    ${CLI_SOURCES}
)

target_include_directories(MyApp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(MyApp PRIVATE
    _CRT_SECURE_NO_WARNINGS
    _USE_MATH_DEFINES
)

target_link_libraries(MyApp PRIVATE
    myapp_core
)

if(WIN32)
    target_link_libraries(MyApp PRIVATE kernel32)
endif()

message(STATUS "=====================================")
message(STATUS "Project: ${PROJECT_NAME}")
message(STATUS "Targets: myapp_core (library) + MyApp (CLI)")
message(STATUS "CMake configure done.")
message(STATUS "=====================================")