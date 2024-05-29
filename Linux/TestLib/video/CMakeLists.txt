add_library_ex(video STATIC
  SOURCES
    avilib.cxx avilib.h
    ffmpeg_decoder.cxx ffmpeg_decoder.h
    image_converter.cxx image_converter.h
    mp4toavi.cxx mp4toavi.h
    video_decoder.cxx video_decoder.h
    video_demuxer.cxx video_demuxer.h
    video_demuxerex.cxx video_demuxerex.h
    video_demuxer_manager.cxx video_demuxer_manager.h
    video_encoder.cxx video_encoder.h
    video_muxer.cxx video_muxer.h
    video_muxerex.cxx video_muxerex.h
    video_transcoder.cxx video_transcoder.h
    video_utils.cxx video_utils.h
    videoconverter.cxx videoconverter.h
  PUBLIC_INCLUDES
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/modules
  PUBLIC_LIBRARIES
    ffmpeg
    libyuv::yuv
    libjpeg-turbo::turbojpeg
)

set(hdrs
  video_utils.h
  video_muxer.h
  video_demuxer.h
  video_demuxer_manager.h
  videoconverter.h
)

install(FILES ${hdrs}
  DESTINATION ${PACKAGE_INSTALL_INCLUDE_DIR}/video
  COMPONENT dev EXCLUDE_FROM_ALL
)
