cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Définissez VITASDK (chemin du SDK Vita)")
  endif()
endif()

project(EternalSiege C CXX)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Eternal Siege")
set(VITA_TITLEID  "ESIE00001")
set(VITA_VERSION  "01.00")
# mémoire étendue pour l'application
set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d ATTRIBUTE2=12")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -ffast-math -mcpu=cortex-a9 -mfpu=neon -fno-strict-aliasing")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -ffast-math -mcpu=cortex-a9 -mfpu=neon -fno-strict-aliasing -Wall -Wno-unused-variable -Wno-class-memaccess -Wno-misleading-indentation")

file(GLOB SOURCES ${CMAKE_SOURCE_DIR}/src/*.cpp)
add_executable(${PROJECT_NAME} ${SOURCES})

target_link_libraries(${PROJECT_NAME}
  vitaGL
  vitashark
  SceShaccCgExt
  mathneon
  taihen_stub
  SceShaccCg_stub
  SceKernelDmacMgr_stub
  SceAppMgr_stub
  SceAppUtil_stub
  SceAudio_stub
  SceCommonDialog_stub
  SceCtrl_stub
  SceDisplay_stub
  SceGxm_stub
  SceIme_stub
  ScePower_stub
  SceSysmodule_stub
  SceTouch_stub
  SceLibKernel_stub
  SceIofilemgr_stub
  m
  stdc++
  pthread
)

vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME} UNSAFE)

# textures photo (CC0) téléchargées à la compilation : facultatives
set(EXTRA_FILES "")
file(GLOB TEX_FILES ${CMAKE_SOURCE_DIR}/build_tex/*.tex)
foreach(tf ${TEX_FILES})
  get_filename_component(tn ${tf} NAME)
  list(APPEND EXTRA_FILES FILE ${tf} tex/${tn})
endforeach()
if(EXISTS ${CMAKE_SOURCE_DIR}/build_tex/CREDITS.txt)
  list(APPEND EXTRA_FILES FILE ${CMAKE_SOURCE_DIR}/build_tex/CREDITS.txt tex/CREDITS.txt)
endif()

vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE sce_sys/icon0.png sce_sys/icon0.png
  FILE sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
  FILE sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
  FILE sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
  ${EXTRA_FILES}
)
