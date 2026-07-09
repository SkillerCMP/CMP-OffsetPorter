cmake_minimum_required(VERSION 3.10)

project(OffsetPorterCpp VERSION 1.0.9 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(OffsetPorter WIN32
    src/main.cpp
    resources/app.rc
)

target_include_directories(OffsetPorter PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/resources")

target_compile_definitions(OffsetPorter PRIVATE
    UNICODE
    _UNICODE
    WINVER=0x0601
    _WIN32_WINNT=0x0601
    _WIN32_IE=0x0700
    NOMINMAX
)

if(MSVC)
    # Keep the app self-contained without adding /MT on top of CMake's default /MD.
    # Adding both causes MSVC D9025 warnings, and the build script logs stderr strictly.
    foreach(flag_var
        CMAKE_CXX_FLAGS
        CMAKE_CXX_FLAGS_DEBUG
        CMAKE_CXX_FLAGS_RELEASE
        CMAKE_CXX_FLAGS_RELWITHDEBINFO
        CMAKE_CXX_FLAGS_MINSIZEREL
    )
        if(DEFINED ${flag_var})
            string(REPLACE "/MDd" "/MTd" ${flag_var} "${${flag_var}}")
            string(REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
        endif()
    endforeach()

    target_compile_options(OffsetPorter PRIVATE
        /W3 /permissive- /EHsc /utf-8
    )
endif()

target_link_libraries(OffsetPorter PRIVATE
    comctl32
    comdlg32
    shell32
    ole32
    gdi32
    user32
)

set_target_properties(OffsetPorter PROPERTIES
    OUTPUT_NAME "OffsetPorter"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
