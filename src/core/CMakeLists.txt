# QuickJS 核心库

# 创建静态库
add_library(quickjs_core STATIC
    quickjs.c
)

# 设置包含目录
target_include_directories(quickjs_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/deps>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# 配置平台特定的编译选项
if(WIN32)
    target_compile_definitions(quickjs_core PRIVATE
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_WARNINGS
        _WIN32_WINNT=0x0601
    )
endif()

# 链接依赖库
target_link_libraries(quickjs_core
    PUBLIC
        quickjs_deps
)

# 链接所需的系统库
if(WIN32)
    target_link_libraries(quickjs_core PUBLIC
        ws2_32
    )
elseif(UNIX AND NOT APPLE)
    target_link_libraries(quickjs_core PUBLIC
        m
        pthread
        dl
    )
elseif(APPLE)
    target_link_libraries(quickjs_core PUBLIC
        pthread
        dl
    )
endif()

# 安装规则
install(TARGETS quickjs_core
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
