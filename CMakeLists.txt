cmake_minimum_required(VERSION 3.24)
project(3sx LANGUAGES C CXX)

include(${PROJECT_SOURCE_DIR}/cmake/imgui_sources.cmake)

set(THREESX_CONFIGURATIONS Debug Release RelWithDebInfo MinSizeRel)
get_property(THREESX_IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(THREESX_IS_MULTI_CONFIG)
    set(CMAKE_CONFIGURATION_TYPES "${THREESX_CONFIGURATIONS}" CACHE STRING "Available build types" FORCE)
else()
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "${THREESX_CONFIGURATIONS}")
endif()

# ======================================
# Options and platform setup
# ======================================

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT PSP AND NOT VITA)
    option(THREESX_STATCHECK "Build with replay statcheck support" OFF)
    option(THREESX_ARCADE_ROM_TEXTURES "Build experimental character textures from CPS3 ROM at runtime" OFF)
endif()

# ======================================
# Include directories
# ======================================

include_directories(
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/imgui/imgui
    ${PROJECT_SOURCE_DIR}/src/imgui/dcimgui
    ${PROJECT_SOURCE_DIR}/src/sdk
    ${PROJECT_SOURCE_DIR}/src/platform/video/opengl/glad
)

# ======================================
# Source collection
# ======================================

file(GLOB_RECURSE GAME_SRC CONFIGURE_DEPENDS src/*.c)

add_executable(3sx MACOSX_BUNDLE
    ${GAME_SRC}
)

if(NOT PSP AND NOT VITA)
    target_sources(3sx PRIVATE
        $<$<CONFIG:Debug>:${IMGUI_SOURCES}>
    )
endif()

# ======================================
# Compiler and linker flags
# ======================================

if(PSP)
    target_compile_definitions(3sx PRIVATE
        $<$<CONFIG:Debug>:DEBUG>

        $<$<CONFIG:Release>:RELEASE>

        CRS_APP_DRIVER_PSP
        CRS_VIDEO_DRIVER_PSP
        CRS_INPUT_DRIVER_SDL
    )
elseif(VITA)
    target_compile_definitions(3sx PRIVATE
        $<$<CONFIG:Debug>:DEBUG>

        $<$<CONFIG:Release>:RELEASE>

        CRS_APP_DRIVER_VITA
        CRS_VIDEO_DRIVER_VITA
        CRS_INPUT_DRIVER_VITA

        SOUND_ENABLED
    )
else()
    target_compile_definitions(3sx PRIVATE
        $<$<CONFIG:Debug>:DEBUG>
        $<$<CONFIG:Debug>:NETPLAY_ENABLED>

        $<$<CONFIG:Release>:RELEASE>
        $<$<AND:$<CONFIG:Release>,$<NOT:$<BOOL:${THREESX_STATCHECK}>>>:CHECKSUM>
        
        $<$<BOOL:${THREESX_STATCHECK}>:STATCHECK>

        # GekkoNet is a pre-built static lib; these were previously propagated by its cmake target
        GEKKONET_STATIC
        GEKKONET_NO_ASIO

        CRS_APP_DRIVER_SDL
        CRS_VIDEO_DRIVER_SDL_GENERIC
        CRS_VIDEO_DRIVER_SDL_GPU
        CRS_VIDEO_DRIVER_OPENGL
        $<$<NOT:$<BOOL:${THREESX_STATCHECK}>>:CRS_INPUT_DRIVER_SDL>
        $<$<BOOL:${THREESX_STATCHECK}>:CRS_INPUT_DRIVER_STATCHECK>

        $<$<BOOL:${THREESX_ARCADE_ROM_TEXTURES}>:ARCADE_ROM_TEXTURES>
        SOUND_ENABLED
        $<$<CONFIG:Debug>:IMGUI>

        GL_SILENCE_DEPRECATION
    )
endif()

if(CMAKE_C_COMPILER_ID STREQUAL "Clang" OR CMAKE_C_COMPILER_ID STREQUAL "AppleClang")
    set(DISABLED_WARNINGS
        -Wpointer-to-int-cast
        -Wno-deprecated-non-prototype
        -Wno-tautological-overlap-compare
    )
elseif(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set(DISABLED_WARNINGS
        -Wno-unused-but-set-variable
        -Wno-parentheses
        -Wno-discarded-qualifiers
        -Wno-format-overflow
        -Wno-comment
        -Wno-maybe-uninitialized
        -Wno-array-bounds
        -Wno-aggressive-loop-optimizations
        -Wno-stringop-overflow
        -Wno-restrict
        -Wno-stringop-truncation
    )
endif()

target_compile_options(3sx PRIVATE
    -Wall
    -Werror

    -fno-strict-aliasing

    ${DISABLED_WARNINGS}

    $<$<OR:$<CONFIG:Debug>,$<BOOL:${THREESX_STATCHECK}>>:-Wno-unused-function>
    # $<$<CONFIG:Debug>:-Wno-unused-variable>
    -Wno-unused-variable
    $<$<OR:$<CONFIG:Debug>,$<BOOL:${THREESX_STATCHECK}>>:-Wno-unused-but-set-variable>
)

if(NOT PSP AND NOT VITA AND (CMAKE_C_COMPILER_ID STREQUAL "Clang" OR CMAKE_C_COMPILER_ID STREQUAL "AppleClang" OR CMAKE_C_COMPILER_ID STREQUAL "GNU"))
    target_compile_options(3sx PRIVATE
        $<$<CONFIG:Debug>:-fsanitize=address>
        $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
    )
    target_link_options(3sx PRIVATE
        $<$<CONFIG:Debug>:-fsanitize=address>
    )
endif()

if(PSP)
    target_compile_options(3sx PRIVATE
        -G0
        -Wno-format-truncation
    )
    target_link_options(3sx PRIVATE -G0)
endif()

set_target_properties(3sx PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/cmake/Info.plist"
)

if(APPLE)
    set_target_properties(3sx PROPERTIES
        OUTPUT_NAME "3SX"
        MACOSX_BUNDLE_BUNDLE_NAME "3SX"
        INSTALL_RPATH "@executable_path/../Frameworks"
    )
elseif(UNIX AND NOT APPLE)
    set_target_properties(3sx PROPERTIES
        INSTALL_RPATH "\$ORIGIN/../lib"
    )
endif()

# ======================================
# Dependencies
# ======================================

set(THIRD_PARTY_DIR "${CMAKE_SOURCE_DIR}/third_party")

find_package(ZLIB REQUIRED)

if(VITA)
    # SDL3 and vita2d come with VitaSDK. Only minizip-ng is built from source (tools/vita/build-minizip.sh).
    # It lives in its own folder so that a desktop build can share the checkout
    set(MINIZIP_NG_ROOT "${THIRD_PARTY_DIR}/vita/minizip-ng/build")

    include_directories(
        ${MINIZIP_NG_ROOT}/include
    )
else()
    find_package(OpenGL REQUIRED)

    set(SDL3_ROOT          "${THIRD_PARTY_DIR}/sdl3/build")
    set(LIBCDIO_ROOT       "${THIRD_PARTY_DIR}/libcdio/build")
    set(GEKKONET_ROOT      "${THIRD_PARTY_DIR}/GekkoNet/build")
    set(SDL3_NET_ROOT      "${THIRD_PARTY_DIR}/SDL_net/build")
    set(MINIZIP_NG_ROOT    "${THIRD_PARTY_DIR}/minizip-ng/build")
    set(TF_PSA_CRYPTO_ROOT "${THIRD_PARTY_DIR}/tf-psa-crypto/build")

    include_directories(
        ${SDL3_ROOT}/include
        ${LIBCDIO_ROOT}/include
        ${GEKKONET_ROOT}/include
        ${SDL3_NET_ROOT}/include
        ${MINIZIP_NG_ROOT}/include
        ${TF_PSA_CRYPTO_ROOT}/include
    )
endif()

if(PSP)
    target_link_libraries(3sx PRIVATE
        m
        "${SDL3_ROOT}/lib/libSDL3.a"
        "${MINIZIP_NG_ROOT}/lib/libminizip-ng.a"
        GL
        ZLIB::ZLIB
        pspdebug
        pspvram
        pspaudio
        pspvfpu
        pspgu
        pspge
        pspdisplay
        psphprm
        pspctrl
        psppower
    )
elseif(VITA)
    target_link_libraries(3sx PRIVATE
        vita2d
        SDL3
        "${MINIZIP_NG_ROOT}/lib/libminizip-ng.a"
        ZLIB::ZLIB
        m
        SceGxm_stub
        SceDisplay_stub
        SceCtrl_stub
        SceAppMgr_stub
        SceAppUtil_stub
        SceAudio_stub
        SceAudioIn_stub
        SceSysmodule_stub
        SceIofilemgr_stub
        SceCommonDialog_stub
        SceTouch_stub
        SceHid_stub
        SceMotion_stub
        ScePower_stub
        SceProcessmgr_stub
        SceCamera_stub
        ScePgf_stub
        ScePvf_stub
    )
else()
    target_link_libraries(3sx PRIVATE
        m
        "${LIBCDIO_ROOT}/lib/libiso9660.a"
        "${LIBCDIO_ROOT}/lib/libcdio.a"
        "${GEKKONET_ROOT}/lib/libGekkoNet.a"
        "${SDL3_NET_ROOT}/lib/libSDL3_net.a"
        "${MINIZIP_NG_ROOT}/lib/libminizip-ng.a"
        "${TF_PSA_CRYPTO_ROOT}/lib/libtfpsacrypto.a"
        ZLIB::ZLIB
        OpenGL::GL
        stdc++ # For GekkoNet
    )
endif()

if(APPLE)
    target_link_libraries(3sx PRIVATE
        ${SDL3_ROOT}/lib/libSDL3.0.dylib
        iconv
    )
elseif(WIN32)
    target_link_libraries(3sx PRIVATE
        ${SDL3_ROOT}/lib/libSDL3.dll.a
        iconv
        bcrypt
		dbghelp
        ws2_32
        iphlpapi
    )
    target_link_options(3sx PRIVATE
        "$<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:--for-linker>"
        "$<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:--pdb=3sx.pdb>"
    )
elseif(UNIX AND NOT VITA)
    target_link_libraries(3sx PRIVATE
        ${SDL3_ROOT}/lib/libSDL3.so
    )
endif()

# ======================================
# Installation
# ======================================

set(PROJECT_LICENSE_FILES
    "${CMAKE_SOURCE_DIR}/LICENSE"
    "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.txt"
)

set(SDL_GPU_SHADER_DIR "${CMAKE_SOURCE_DIR}/src/platform/video/sdl_gpu/shaders/compiled")
set(OPENGL_SHADER_DIR "${CMAKE_SOURCE_DIR}/src/platform/video/opengl/shaders")
set(ASSETS_DIR "${CMAKE_SOURCE_DIR}/assets")

if(APPLE)
    install(TARGETS 3sx
        RUNTIME DESTINATION bin
        BUNDLE DESTINATION .
    )

    install(FILES
        ${SDL3_ROOT}/lib/libSDL3.0.dylib
        DESTINATION 3SX.app/Contents/Frameworks
    )

    install(FILES
        ${PROJECT_LICENSE_FILES}
        DESTINATION 3SX.app/Contents/Resources/licenses
    )

    install(CODE "file(REMOVE_RECURSE \"\${CMAKE_INSTALL_PREFIX}/3SX.app/Contents/Resources/shaders/sdlgpu/compiled\")")

    install(DIRECTORY "${SDL_GPU_SHADER_DIR}/"
        DESTINATION 3SX.app/Contents/Resources/shaders/sdlgpu
    )

    install(DIRECTORY "${OPENGL_SHADER_DIR}/"
        DESTINATION 3SX.app/Contents/Resources/shaders/opengl
    )

    install(DIRECTORY "${ASSETS_DIR}/"
        DESTINATION 3SX.app/Contents/Resources/assets
    )
elseif(WIN32)
    install(TARGETS 3sx
		RUNTIME_DEPENDENCY_SET deps
        RUNTIME DESTINATION bin
    )

    execute_process(COMMAND cygpath -m /mingw64/bin
		OUTPUT_VARIABLE binPath
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)

	# automatically copies all the dependent DLLS, and ignores the system ones because they aren't necessary
	install(RUNTIME_DEPENDENCY_SET deps
		DIRECTORIES ${binPath} "${SDL3_ROOT}/bin"
		PRE_EXCLUDE_REGEXES "^api"
		POST_EXCLUDE_REGEXES ".*system32/.*\\.dll"
		DESTINATION bin
	)

    install(FILES
        "$<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:${CMAKE_CURRENT_BINARY_DIR}/3sx.pdb>"
        DESTINATION bin
    )

    install(FILES
        ${PROJECT_LICENSE_FILES}
        DESTINATION bin/licenses
    )

    install(CODE "file(REMOVE_RECURSE \"\${CMAKE_INSTALL_PREFIX}/bin/shaders/sdlgpu/compiled\")")

    install(DIRECTORY "${SDL_GPU_SHADER_DIR}/"
        DESTINATION bin/shaders/sdlgpu
    )

    install(DIRECTORY "${OPENGL_SHADER_DIR}/"
        DESTINATION bin/shaders/opengl
    )

    install(DIRECTORY "${ASSETS_DIR}/"
        DESTINATION bin/assets
    )
elseif(UNIX AND NOT VITA)
    install(TARGETS 3sx
        RUNTIME DESTINATION bin
    )

    file(GLOB SDL3_SO "${SDL3_ROOT}/lib/libSDL3.so*")

    install(FILES
        ${SDL3_SO}
        DESTINATION lib
    )

    install(FILES
        ${PROJECT_LICENSE_FILES}
        DESTINATION share/3sx/licenses
    )

    install(CODE "file(REMOVE_RECURSE \"\${CMAKE_INSTALL_PREFIX}/bin/shaders/sdlgpu/compiled\")")

    install(DIRECTORY "${SDL_GPU_SHADER_DIR}/"
        DESTINATION bin/shaders/sdlgpu
    )

    install(DIRECTORY "${OPENGL_SHADER_DIR}/"
        DESTINATION bin/shaders/opengl
    )

    install(DIRECTORY "${ASSETS_DIR}/"
        DESTINATION bin/assets
    )
endif()

# ======================================
# Packaging
# ======================================

if(PSP)
    create_pbp_file(
        TARGET 3sx
        ICON_PATH NULL
        BACKGROUND_PATH NULL
        MUSIC_PATH NULL
        PREVIEW_PATH NULL
        TITLE "3SX"
        VERSION 0.1
    )
elseif(VITA)
    include("${VITASDK}/share/vita.cmake" REQUIRED)

    set(VITA_APP_NAME "3SX")
    set(VITA_TITLE_ID "TSXV00001")
    set(VITA_VERSION "01.00")
    set(VITA_SCE_SYS_DIR "${CMAKE_SOURCE_DIR}/tools/vita/sce_sys")
    set(VITA_MKSFOEX_FLAGS "-d PARENTAL_LEVEL=1")

    vita_create_self(3sx.self 3sx)
    vita_create_vpk(3sx.vpk ${VITA_TITLE_ID} 3sx.self
        VERSION ${VITA_VERSION}
        NAME ${VITA_APP_NAME}
        FILE ${ASSETS_DIR}/glyphs.bmp assets/glyphs.bmp
        FILE ${ASSETS_DIR}/xbox_buttons.bmp assets/xbox_buttons.bmp
        FILE ${VITA_SCE_SYS_DIR}/icon0.png sce_sys/icon0.png
        FILE ${VITA_SCE_SYS_DIR}/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
        FILE ${VITA_SCE_SYS_DIR}/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
        FILE ${VITA_SCE_SYS_DIR}/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
        FILE ${CMAKE_SOURCE_DIR}/LICENSE licenses/LICENSE
        FILE ${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.txt licenses/THIRD_PARTY_NOTICES.txt
    )
else()
    set(CPACK_PACKAGE_NAME "3SX")
    set(CPACK_PACKAGE_VENDOR "CrowdedStreet")
    # set(CPACK_PACKAGE_VERSION "1.0.0")

    if(WIN32)
        set(CPACK_GENERATOR "ZIP")
    elseif(APPLE)
        set(CPACK_GENERATOR "DragNDrop")
    else(UNIX)
        set(CPACK_GENERATOR "TGZ")
    endif()

    include(CPack)
endif()
