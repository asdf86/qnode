# QuickJS 依赖库

# 创建静态库
add_library(quickjs_deps STATIC
    cutils.c
    dtoa.c
    libregexp.c
    libunicode.c
)

# 设置包含目录
target_include_directories(quickjs_deps
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# 配置平台特定的编译选项
if(WIN32)
    target_compile_definitions(quickjs_deps PRIVATE
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_WARNINGS
        _WIN32_WINNT=0x0601
    )
endif()

# 链接所需的系统库
if(WIN32)
    target_link_libraries(quickjs_deps PUBLIC
    )
elseif(UNIX AND NOT APPLE)
    target_link_libraries(quickjs_deps PUBLIC
        m
        pthread
    )
elseif(APPLE)
    target_link_libraries(quickjs_deps PUBLIC
        pthread
    )
endif()

# 安装规则
install(TARGETS quickjs_deps
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
