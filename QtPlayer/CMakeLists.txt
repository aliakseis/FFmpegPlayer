cmake_minimum_required(VERSION 3.5)

project(QtPlayer LANGUAGES CXX)

option(DEVELOPER_OPENGL "Enables opengl display" ON)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
  add_definitions(-D_WIN32_WINNT=0x600)
endif()

if(DEVELOPER_OPENGL)
        add_definitions(-DDEVELOPER_OPENGL)
endif(DEVELOPER_OPENGL)

add_definitions(-DBOOST_LOG_DYN_LINK)

# QtCreator supports the following variables for Android, which are identical to qmake Android variables.
# Check http://doc.qt.io/qt-5/deployment-android.html for more information.
# They need to be set before the find_package(Qt5 ...) call.

#if(ANDROID)
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
#    if (ANDROID_ABI STREQUAL "armeabi-v7a")
#        set(ANDROID_EXTRA_LIBS
#            ${CMAKE_CURRENT_SOURCE_DIR}/path/to/libcrypto.so
#            ${CMAKE_CURRENT_SOURCE_DIR}/path/to/libssl.so)
#    endif()
#endif()

find_package(QT NAMES Qt6 Qt5 COMPONENTS Widgets REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Widgets REQUIRED)

find_package(Boost REQUIRED thread log)

find_package(portaudio CONFIG)
if(NOT PORTAUDIO_FOUND)
    find_path(PORTAUDIO_INCLUDE_DIR
          NAMES portaudio.h
          DOC "The PortAudio include directory"
    )
    find_library(PORTAUDIO_LIBRARY
             NAMES portaudio
             DOC "The PortAudio library"
    )
    include(FindPackageHandleStandardArgs)
    find_package_handle_standard_args(PortAudio
        REQUIRED_VARS PORTAUDIO_LIBRARY PORTAUDIO_INCLUDE_DIR
    )
    if(PORTAUDIO_FOUND)
        set(PORTAUDIO_LIBRARIES ${PORTAUDIO_LIBRARY})
        set(PORTAUDIO_INCLUDE_DIRS ${PORTAUDIO_INCLUDE_DIR})
    endif()
endif()


find_path(AVCODEC_INCLUDE_DIR libavcodec/avcodec.h)

if (AVCODEC_INCLUDE_DIR)

  find_library(AVCODEC_LIBRARY avcodec)

  find_path(AVFORMAT_INCLUDE_DIR libavformat/avformat.h)
  find_library(AVFORMAT_LIBRARY avformat)

  find_path(AVUTIL_INCLUDE_DIR libavutil/avutil.h)
  find_library(AVUTIL_LIBRARY avutil)

  find_path(AVDEVICE_INCLUDE_DIR libavdevice/avdevice.h)
  find_library(AVDEVICE_LIBRARY avdevice)

  find_path(SWSCALE_INCLUDE_DIR libswscale/swscale.h)
  find_library(SWSCALE_LIBRARY swscale)

  find_path(SWRESAMPLE_INCLUDE_DIR libswresample/swresample.h)
  find_library(SWRESAMPLE_LIBRARY swresample)

else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
    libavdevice
    libavfilter
    libavformat
    libavcodec
    libswresample
    libswscale
    libavutil
  )
endif()

set(SOURCES
    customdockwidget.cpp
    customdockwidget.h
    ffmpegdecoder.cpp
    ffmpegdecoder.h
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    mousehoverbutton.cpp
    mousehoverbutton.h
    opengldisplay.cpp
    opengldisplay.h   
    portaudioplayer.cpp
    portaudioplayer.h
    videocontrol.cpp
    videocontrol.h
    videocontrol.ui
    videodisplay.cpp
    videodisplay.h
    videoplayer.cpp
    videoplayer.h
    videoplayerwidget.cpp
    videoplayerwidget.h
    videoprogressbar.cpp
    videoprogressbar.h
    videowidget.cpp
    videowidget.h
    volumeprogressbar.cpp
    volumeprogressbar.h
    widgetdisplay.cpp
    widgetdisplay.h)


add_executable(QtPlayer ${SOURCES} ${CMAKE_SOURCE_DIR}/resources/resources.qrc)


file(GLOB VIDEO_SRCS ../video/*.cpp)
add_library(video STATIC ${VIDEO_SRCS})

target_include_directories(video PRIVATE ../video)

if (AVCODEC_INCLUDE_DIR)
    target_include_directories(video PRIVATE
        ${AVCODEC_INCLUDE_DIR}
        ${AVFORMAT_INCLUDE_DIR}
        ${AVUTIL_INCLUDE_DIR}
        ${AVDEVICE_INCLUDE_DIR}
        ${SWSCALE_INCLUDE_DIR}
        ${SWRESAMPLE_INCLUDE_DIR}
    )
endif()


target_link_libraries(video ${Boost_LIBRARIES})
if (AVCODEC_INCLUDE_DIR)
    target_link_libraries(video 
        ${AVCODEC_LIBRARY}
        ${AVFORMAT_LIBRARY}
        ${AVUTIL_LIBRARY}
        ${AVDEVICE_LIBRARY}
        ${SWSCALE_LIBRARY}
        ${SWRESAMPLE_LIBRARY}
        )
else()
    target_link_libraries(video 
        PkgConfig::LIBAV
        )
endif()

#file(GLOB AUDIO_SRCS ../audio/*.cpp)
#add_library(audio STATIC ${AUDIO_SRCS})
#target_include_directories(audio PRIVATE ../audio)
#target_link_libraries(audio)


target_link_libraries(QtPlayer PRIVATE Qt${QT_VERSION_MAJOR}::Widgets video portaudio)
