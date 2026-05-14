# QuickJS 标准库

# 创建静态库
add_library(quickjs_libc STATIC
    quickjs-libc.c
    process.c
    fs.c
)

# 设置包含目录
target_include_directories(quickjs_libc
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/deps>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/core>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# 配置平台特定的编译选项
if(WIN32)
    target_compile_definitions(quickjs_libc PRIVATE
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_WARNINGS
        _WIN32_WINNT=0x0601
    )
endif()

# 链接核心库
target_link_libraries(quickjs_libc
    PUBLIC
        quickjs_core
)

# 链接所需的系统库
if(WIN32)
    target_link_libraries(quickjs_libc PUBLIC
        ws2_32
    )
elseif(UNIX AND NOT APPLE)
    target_link_libraries(quickjs_libc PUBLIC
        pthread
    )
elseif(APPLE)
    target_link_libraries(quickjs_libc PUBLIC
        pthread
    )
endif()

# 安装规则
install(TARGETS quickjs_libc
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
