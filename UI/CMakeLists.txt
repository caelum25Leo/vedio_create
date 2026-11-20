
cmake_minimum_required(VERSION 3.10)

project(testCode)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt 5.7
set(CMAKE_PREFIX_PATH "C:/Qt/Qt5.7.0/5.7/msvc2015")

find_package(Qt5 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
)

add_executable(testCode
    tc.cpp
    briefwindow.cpp
    previewdialog.cpp
    briefmarkdown.cpp
    boardscan.cpp
    stylecatalog.cpp
)

target_link_libraries(testCode PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
)

set_target_properties(testCode PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
)

# Make MSVC treat the source & execution charset as UTF-8.
# (add_compile_options() only affects targets created AFTER it, so use target_compile_options to be effective.)
if(MSVC)
    target_compile_options(testCode PRIVATE /utf-8)
endif()

if(MSVC)
    add_compile_options(/utf-8)
endif()

# =============================================================================
# 引用 MDBMD64.dll(工程数据库模块 / GIS 的 MDB)的配置
#
# 先看清楚这个 DLL 是什么(dumpbin 实测结果):
#   * x64、MSVC 生成的原生 C++ DLL;1731 个导出全部是 C++ 修饰名,没有 C 接口;
#   * 它自己还依赖 NETMD64.dll / ISFMD64.dll / ACEMD64.dll 和 Debug CRT
#     (MSVCP140D.dll / VCRUNTIME140D.dll / ucrtbased.dll)
#     —— 即这个 MDBMD64.dll 是「Debug(/MDd)构建」,名字里的 MD(Debug)/MR(Release)
#        就是这套库的命名约定,所以链接它的工程要用 Debug 配置(/MDd,本工程 Debug 的默认值)。
#
# 因此「引用」它不是只把 DLL 放在那里就行,需要三样东西各就各位:
#   1) 导入库 MDBMD64.lib —— 链接期告诉链接器「这些函数在 MDBMD64.dll 里」
#                            (MDBMD_LIB_DIR,默认 <工程>/lib;工程里没有才用 D:/code/lib)
#   2) 头文件            —— 编译期拿声明(MDBMD_INCLUDE_DIR,可留空;
#                            没有官方头文件时见 mdbmd_api.h 的手写声明做法)
#   3) 运行时 DLL        —— 启动期能被找到(MDBMD_RUNTIME_DIRS 里的 DLL 会被拷到 exe 同级目录)
#
# 不想自动链接时:cmake -DTESTCODE_USE_MDBMD=OFF ..
# =============================================================================
option(TESTCODE_USE_MDBMD "链接 MDBMD64.lib 并把运行时 DLL 拷贝到 exe 目录" ON)

# 目录默认值:优先用工程自己标准化的 <工程>/lib 和 <工程>/bin(和源码一起走,换机器不用改),
# 找不到才退回 D:/code/lib、D:/code/bin。
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib")
    set(_mdbmd_lib_default "${CMAKE_CURRENT_SOURCE_DIR}/lib")
else()
    set(_mdbmd_lib_default "D:/code/lib")
endif()

set(_mdbmd_rt_defaults "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bin")
    list(APPEND _mdbmd_rt_defaults "${CMAKE_CURRENT_SOURCE_DIR}/bin")
endif()
if(EXISTS "D:/code/bin")
    # MDBMD64.dll 自身依赖的 NETMD64.dll / ISFMD64.dll / ACEMD64.dll 目前还在那里
    list(APPEND _mdbmd_rt_defaults "D:/code/bin")
endif()
if(NOT _mdbmd_rt_defaults)
    list(APPEND _mdbmd_rt_defaults "${CMAKE_CURRENT_SOURCE_DIR}/bin")
endif()

set(MDBMD_LIB_DIR      "${_mdbmd_lib_default}" CACHE PATH   "MDBMD64.lib 所在目录(默认 <工程>/lib)")
set(MDBMD_RUNTIME_DIRS "${_mdbmd_rt_defaults}" CACHE STRING "运行时 DLL 搜索目录(按顺序,默认 <工程>/bin 然后 D:/code/bin)")

# --- 有官方头文件时填这两项(强烈推荐,比 mdbmd_api.h 的手写声明可靠)----------------
# MDBMD_INCLUDE_DIRS:官方头文件所在目录,多个用 ; 分隔
#   例:-DMDBMD_INCLUDE_DIRS="D:/code/MDB/include;D:/code/ThirdParty/Include"
#       (导出表里出现了 ACE_Message_Block / ACE_Thread_Mutex,所以多半还要 ACE 的头目录)
# MDBMD_OFFICIAL_HEADER:官方「主头文件名」(不是路径),例如 MDBMD.h
#   填了它,mdbmd_example.cpp 就 include 它、不再用 mdbmd_api.h 的手写声明(避免类重复定义)
set(MDBMD_INCLUDE_DIRS   "" CACHE STRING "MDBMD 官方头文件目录(多个用 ; 分隔,默认 <工程目录>/include + ThirdParty/Include)")
if(NOT MDBMD_INCLUDE_DIRS)
    set(_mdbmd_inc_defaults "")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include")
        list(APPEND _mdbmd_inc_defaults "${CMAKE_CURRENT_SOURCE_DIR}/include")
    endif()
    if(EXISTS "D:/code/ThirdParty/Include")
        list(APPEND _mdbmd_inc_defaults "D:/code/ThirdParty/Include")   # ACE + ACE_Includes.h 等框架头
    endif()
    set(MDBMD_INCLUDE_DIRS "${_mdbmd_inc_defaults}"
        CACHE STRING "MDBMD 官方头文件目录(多个用 ; 分隔,默认 <工程目录>/include + ThirdParty/Include)" FORCE)
endif()

# 这批老代码的 .h 之间是「不写目录前缀、同目录直接互相 #include」的写法,
# 等价于原 VS 工程里那一长串「附加包含目录」,所以这里把 include 下每个子目录也加进去。
option(TESTCODE_MDBMD_ADD_SUBDIR_INCLUDES "把 include/ 下每个子目录都加入包含路径(兼容老代码写法)" ON)
set(MDBMD_SUBDIR_INCLUDES "")
set(MDBMD_SHADOWED_DIRS "")
set(_mdbmd_subdir_count 0)
# 这些名字与 CRT/标准库同名:含这类头文件的目录绝不能进包含路径,否则会把系统的
# stdint.h / string.h 等遮蔽掉(cstdint 立刻一片 C2039,实测过)。
set(_mdbmd_std_headers
    stdint.h stddef.h stdarg.h stdbool.h stdio.h stdlib.h string.h math.h time.h
    limits.h float.h errno.h wchar.h inttypes.h ctype.h assert.h setjmp.h signal.h
    locale.h conio.h io.h fcntl.h direct.h process.h yvals.h malloc.h new.h
    typeinfo.h exception.h)
if(TESTCODE_MDBMD_ADD_SUBDIR_INCLUDES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include")
    file(GLOB_RECURSE _mdbmd_entries LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/include/*")
    foreach(_e IN LISTS _mdbmd_entries)
        if(NOT IS_DIRECTORY "${_e}")
            continue()
        endif()
        if(_e MATCHES "[/\\\\]\\.(svn|git|hg)")
            continue()
        endif()
        set(_shadow "")
        foreach(_h IN LISTS _mdbmd_std_headers)
            if(EXISTS "${_e}/${_h}")
                set(_shadow "${_h}")
                break()
            endif()
        endforeach()
        if(_shadow)
            list(APPEND MDBMD_SHADOWED_DIRS "${_e} (含 ${_shadow})")
        else()
            list(APPEND MDBMD_SUBDIR_INCLUDES "${_e}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES MDBMD_SUBDIR_INCLUDES)
    list(LENGTH MDBMD_SUBDIR_INCLUDES _mdbmd_subdir_count)
    message(STATUS "MDBMD: 额外加入 ${_mdbmd_subdir_count} 个 include 子目录到包含路径")
    if(MDBMD_SHADOWED_DIRS)
        message(STATUS "MDBMD: 为免遮蔽标准头,已跳过: ${MDBMD_SHADOWED_DIRS}")
    endif()
endif()
set(MDBMD_OFFICIAL_HEADER "" CACHE STRING "MDBMD 官方主头文件名,如 MDBMD.h(留空=用手写声明)")
# 除 MDBMD64.lib 外还要链的导入库(如 ACE 相关),会优先在 MDBMD_LIB_DIR 里找
set(MDBMD_EXTRA_LIBS     "" CACHE STRING "额外的导入库名(多个用 ; 分隔,如 ACEMD64)")

# 这些头文件里有 #pragma comment(lib, ...) 自动链接指令(ACE 会按 _DEBUG 自动要 ACEMD64.lib,
# 还有 SgDataSet 系列等),所以除了工程自己的 lib\ 之外,还要把供应商导入库目录交给链接器
# (等价于 VS 里的「链接器 → 常规 → 附加库目录」)。
set(MDBMD_EXTRA_LIB_DIRS "D:/code/lib" CACHE STRING "额外的导入库搜索目录(供 #pragma comment(lib) 使用,多个用 ; 分隔)")
foreach(_d IN LISTS MDBMD_LIB_DIR MDBMD_EXTRA_LIB_DIRS)
    if(_d AND EXISTS "${_d}")
        set_property(TARGET testCode APPEND PROPERTY LINK_DIRECTORIES "${_d}")
        message(STATUS "MDBMD: 链接器库目录 += ${_d}")
    endif()
endforeach()

# --- 头文件探针:只为「编译期」查还缺哪个 .h,默认关闭 -------------------------
# 用法:cmake -S . -B build -DTESTCODE_MDBMD_HEADER_PROBE=ON && cmake --build build --config Debug
# 报 C1083 打不开的 .h 就是下一个要补的文件。
option(TESTCODE_MDBMD_HEADER_PROBE "只编译 MDBMD 官方头文件,用于查找缺失的头文件" OFF)
# 探针要编译的入口头(相对 MDBMD_INCLUDE_DIRS)。默认就是我们要用的那个:
#   include\nt_unix\csgdataset.h  ->  "nt_unix/csgdataset.h"
set(MDBMD_PROBE_HEADER "nt_unix/csgdataset.h" CACHE STRING "头文件探针编译的入口头")
# 示例程序改用「官方头文件」而不是 mdbmd_api.h 的手写声明(需要官方头里已声明示例用到的那几个函数)
option(TESTCODE_MDBMD_EXAMPLE_USE_OFFICIAL_HEADER "mdbmd_example.cpp 改用官方头文件" OFF)
if(TESTCODE_MDBMD_EXAMPLE_USE_OFFICIAL_HEADER)
    target_compile_definitions(testCode PRIVATE TESTCODE_MDBMD_EXAMPLE_USE_OFFICIAL_HEADER)
endif()
if(TESTCODE_MDBMD_HEADER_PROBE)
    if(MDBMD_INCLUDE_DIRS OR MDBMD_SUBDIR_INCLUDES)
        target_include_directories(testCode PRIVATE ${MDBMD_INCLUDE_DIRS} ${MDBMD_SUBDIR_INCLUDES})
    endif()
    if(MDBMD_OFFICIAL_HEADER)
        target_compile_definitions(testCode PRIVATE MDBMD_OFFICIAL_HEADER="${MDBMD_OFFICIAL_HEADER}")
    endif()
    target_compile_definitions(testCode PRIVATE TESTCODE_MDBMD_HEADER_PROBE)
    if(MDBMD_PROBE_HEADER)
        target_compile_definitions(testCode PRIVATE MDBMD_PROBE_HEADER="${MDBMD_PROBE_HEADER}")
    endif()
    message(STATUS "MDBMD: 头文件探针已开启(TESTCODE_MDBMD_HEADER_PROBE=ON), 入口头 = ${MDBMD_PROBE_HEADER}")
endif()

if(TESTCODE_USE_MDBMD)
    find_library(MDBMD_IMPORT_LIB NAMES MDBMD64 PATHS "${MDBMD_LIB_DIR}" NO_DEFAULT_PATH)

    if(NOT MDBMD_IMPORT_LIB)
        message(WARNING "TESTCODE_USE_MDBMD=ON,但 ${MDBMD_LIB_DIR}/MDBMD64.lib 不存在,"
                        "本次不链接 MDBMD64.dll(可用 -DMDBMD_LIB_DIR=... 指定它在哪)。")
    else()
        message(STATUS "MDBMD: 导入库 = ${MDBMD_IMPORT_LIB}")

        # 链接期:等价于 VS 里「附加依赖项 = MDBMD64.lib」
        target_link_libraries(testCode PRIVATE "${MDBMD_IMPORT_LIB}")
        target_compile_definitions(testCode PRIVATE TESTCODE_HAS_MDBMD)

        # 编译期:官方头文件目录(有就加,没有就用 mdbmd_api.h 里的手写声明)
        if(MDBMD_INCLUDE_DIRS OR MDBMD_SUBDIR_INCLUDES)
            target_include_directories(testCode PRIVATE ${MDBMD_INCLUDE_DIRS} ${MDBMD_SUBDIR_INCLUDES})
            message(STATUS "MDBMD: 头文件目录 = ${MDBMD_INCLUDE_DIRS} (+${_mdbmd_subdir_count} 个子目录)")
        endif()
        if(MDBMD_OFFICIAL_HEADER)
            target_compile_definitions(testCode PRIVATE MDBMD_OFFICIAL_HEADER="${MDBMD_OFFICIAL_HEADER}")
            message(STATUS "MDBMD: 使用官方头文件 <${MDBMD_OFFICIAL_HEADER}>(不再用手写声明)")
        else()
            message(STATUS "MDBMD: 未指定官方头文件,使用 mdbmd_api.h 的手写声明")
        endif()

        # 除 MDBMD64.lib 之外的导入库(例如用到了 ACE 接口就要 ACEMD64.lib)
        foreach(_lib IN LISTS MDBMD_EXTRA_LIBS)
            find_library(_mdbmd_extra_${_lib} NAMES ${_lib}
                         PATHS "${MDBMD_LIB_DIR}" NO_DEFAULT_PATH)
            if(_mdbmd_extra_${_lib})
                target_link_libraries(testCode PRIVATE "${_mdbmd_extra_${_lib}}")
                message(STATUS "MDBMD: 额外链接 = ${_mdbmd_extra_${_lib}}")
            else()
                message(WARNING "额外库 ${_lib} 在 ${MDBMD_LIB_DIR} 里没找到,按名字交给链接器。")
                target_link_libraries(testCode PRIVATE "${_lib}")
            endif()
        endforeach()

        # 运行期:MDBMD64.dll 本体 + 它 import 表里的三个 GIS 模块,都放到 exe 同级目录
        # (等于 VS 里「生成后事件」做 xcopy;也可以换成把 DLL 目录加进 PATH)
        set(MDBMD_RUNTIME_DLLS MDBMD64.dll NETMD64.dll ISFMD64.dll ACEMD64.dll)
        foreach(_dll IN LISTS MDBMD_RUNTIME_DLLS)
            set(_src "")
            foreach(_dir IN LISTS MDBMD_RUNTIME_DIRS)
                if(EXISTS "${_dir}/${_dll}")
                    set(_src "${_dir}/${_dll}")
                    break()
                endif()
            endforeach()
            if(_src)
                add_custom_command(TARGET testCode POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${_src}" "$<TARGET_FILE_DIR:testCode>"
                    COMMENT "拷贝 ${_dll} 到 $<TARGET_FILE_DIR:testCode>")
            else()
                message(WARNING "找不到 ${_dll}(已查找:${MDBMD_RUNTIME_DIRS}),"
                                "程序启动时会报 0xc0000135(找不到 DLL)。")
            endif()
        endforeach()
    endif()
endif()