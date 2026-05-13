cmake_minimum_required(VERSION 3.15)
project(qnode VERSION 1.0.0 LANGUAGES C)

# 设置C标准
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 跨平台配置
# 平台检测
if(WIN32)
    set(PLATFORM_WINDOWS TRUE)
    set(PLATFORM_NAME "Windows")
    # For MSVC, ensure we have intrin.h for bit operations
    if(MSVC)
        set(CMAKE_REQUIRED_INCLUDES ${CMAKE_REQUIRED_INCLUDES} "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.50.35717/include")
    endif()
elseif(APPLE)
    set(PLATFORM_MACOS TRUE)
    set(PLATFORM_NAME "macOS")
elseif(UNIX)
    set(PLATFORM_UNIX TRUE)
    set(PLATFORM_NAME "Unix")
endif()

# 配置编译选项
if(MSVC)
    # Windows/MSVC 特定选项
    add_compile_options(/W4)
    add_compile_options(/utf-8)  # UTF-8 source files
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    add_compile_definitions(_CRT_NONSTDC_NO_WARNINGS)
    add_compile_definitions(_WIN32_WINNT=0x0601)  # Windows 7 or later
    # 禁用MSVC警告
    add_compile_options(/wd4244 /wd4267 /wd4996)
else()
    # GCC/Clang 特定选项 - 基于QuickJS Makefile
    add_compile_options(-Wall -Wextra -Wno-sign-compare -Wno-missing-field-initializers)
	add_compile_options(-Wno-unused-parameter -Wno-array-bounds -Wno-format-truncation -Wno-infinite-recursion -fwrapv)
    # macOS 特定选项
    if(APPLE)
        add_compile_options(-Wno-deprecated-declarations)
        add_link_options(-Wl,-search_paths_first)
    endif()
    # Linux 特定选项
    if(UNIX AND NOT APPLE)
        add_link_options(-Wl,--as-needed)
    endif()
endif()

# 配置QuickJS选项
option(CONFIG_BIGNUM "Enable bignum support" OFF)
option(USE_WORKER "Enable Worker API" OFF)
option(CONFIG_VERSION "Enable version string" ON)
option(ENABLE_DEBUG "Enable debug output" OFF)
option(ENABLE_LTO "Enable link-time optimization" OFF)

# 添加定义
add_compile_definitions(
    $<$<BOOL:${CONFIG_BIGNUM}>:CONFIG_BIGNUM>
    $<$<BOOL:${USE_WORKER}>:USE_WORKER>
    $<$<BOOL:${CONFIG_VERSION}>:CONFIG_VERSION="${PROJECT_VERSION}">
    $<$<BOOL:${ENABLE_DEBUG}>:CONFIG_DEBUG=1>
)

# Link-time optimization
if(ENABLE_LTO AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(MSVC)
        add_compile_options(/GL)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /LTCG")
    else()
        add_compile_options(-flto)
        add_link_options(-flto)
    endif()
endif()

# 添加子目录
add_subdirectory(src/deps)
add_subdirectory(src/core)
add_subdirectory(src/lib)

# 编译qjsc.exe
add_executable(qjsc_exe src/qjsc.c)
target_include_directories(qjsc_exe PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/deps
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lib
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Windows特定选项
if(WIN32 AND NOT MSVC)
    target_compile_definitions(qjsc_exe PRIVATE
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_WARNINGS
        _WIN32_WINNT=0x0601
        __USE_MINGW_ANSI_STDIO
    )
endif()

# 链接库
target_link_libraries(qjsc_exe PRIVATE
    quickjs_core
    quickjs_deps
    quickjs_libc
)

# 设置qjsc.exe输出路径
set_target_properties(qjsc_exe PROPERTIES
    RUNTIME_OUTPUT_NAME qjsc
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Windows平台链接库
if(WIN32)
    # 静态链接pthread和系统库，避免依赖DLL
    if(NOT MSVC)
        target_link_options(qjsc_exe PRIVATE -static -static-libgcc -static-libstdc++)
        target_link_libraries(qjsc_exe PRIVATE ws2_32 pthread)
    else()
        target_link_libraries(qjsc_exe PRIVATE ws2_32)
    endif()
elseif(UNIX AND NOT APPLE)
    target_link_libraries(qjsc_exe PRIVATE pthread dl m)
elseif(APPLE)
    target_link_libraries(qjsc_exe PRIVATE pthread dl)
endif()

# 生成repl.c
set(REPL_JS_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/repl.js")
set(REPL_C_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/repl.c")

add_custom_command(
    OUTPUT ${REPL_C_PATH}
    COMMAND ${CMAKE_BINARY_DIR}/bin/qjsc${CMAKE_EXECUTABLE_SUFFIX} -s -c -o ${REPL_C_PATH} -m ${REPL_JS_PATH}
    DEPENDS qjsc_exe ${REPL_JS_PATH}
    COMMENT "Generating repl.c from repl.js using qjsc"
    VERBATIM
)

# 自定义目标用于生成repl.c
add_custom_target(generate_repl_c
    DEPENDS ${REPL_C_PATH}
)

# 创建qnode和qjs可执行文件，包括repl.c
add_executable(qnode src/qnode.c ${REPL_C_PATH})
add_dependencies(qnode generate_repl_c)

# 创建qjs可执行文件（与qnode相同的文件）
add_executable(qjs src/qnode.c ${REPL_C_PATH})
add_dependencies(qjs generate_repl_c)

# 设置包含目录
target_include_directories(qnode
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/deps
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/lib
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(qjs
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/deps
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/lib
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 配置平台特定的编译选项
if(WIN32)
    target_compile_definitions(qnode PRIVATE
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_WARNINGS
        _WIN32_WINNT=0x0601
        __USE_MINGW_ANSI_STDIO  # 根据QuickJS Makefile
    )
    target_compile_definitions(qjs PRIVATE
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_WARNINGS
        _WIN32_WINNT=0x0601
        __USE_MINGW_ANSI_STDIO
    )
if(MSVC)
    add_compile_options(/utf-8)
endif()

endif()

# 链接库
target_link_libraries(qnode
    PRIVATE
        quickjs_core
        quickjs_deps
        quickjs_libc
)

target_link_libraries(qjs
    PRIVATE
        quickjs_core
        quickjs_deps
        quickjs_libc
)

# 设置输出目录
set_target_properties(qnode PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

set_target_properties(qjs PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# 平台特定的链接库
if(WIN32)
    # 静态链接pthread和系统库，避免依赖DLL
    if(NOT MSVC)
        target_link_options(qnode PRIVATE -static -static-libgcc -static-libstdc++)
        target_link_options(qjs PRIVATE -static -static-libgcc -static-libstdc++)
        target_link_libraries(qnode PRIVATE ws2_32 pthread)
        target_link_libraries(qjs PRIVATE ws2_32 pthread)
    else()
        target_link_libraries(qnode PRIVATE ws2_32)
        target_link_libraries(qjs PRIVATE ws2_32)
    endif()
elseif(UNIX AND NOT APPLE)
    target_link_libraries(qnode PRIVATE pthread dl m)
    target_link_libraries(qjs PRIVATE pthread dl m)
elseif(APPLE)
    target_link_libraries(qnode PRIVATE pthread dl)
    target_link_libraries(qjs PRIVATE pthread dl)
endif()

# 安装规则
include(GNUInstallDirs)

install(TARGETS qnode
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES include/quickjs.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# 打印配置信息
message(STATUS "QNode Configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  Platform: ${PLATFORM_NAME}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C Standard: ${CMAKE_C_STANDARD}")
message(STATUS "  C Compiler: ${CMAKE_C_COMPILER_ID}")
message(STATUS "  Options:")
message(STATUS "    Bignum Support: ${CONFIG_BIGNUM}")
message(STATUS "    Worker API: ${USE_WORKER}")
message(STATUS "    Debug Output: ${ENABLE_DEBUG}")
message(STATUS "    LTO: ${ENABLE_LTO}")
