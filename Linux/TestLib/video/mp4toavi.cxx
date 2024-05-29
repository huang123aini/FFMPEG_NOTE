
#include "mp4toavi.h"
extern "C" {
#include "libavutil/error.h"
}
#include "common/string_util.h"
#include "common/taskqueue.h"
#include "turbojpeg.h"
#include <experimental/filesystem>
#include <iostream>

int jpeg_compress(const unsigned char *data, int width, int height,
                  int pixelFormat, unsigned char **jpegBuf,
                  unsigned long *jpegSize, int jpegSubsamp, int jpegQual,
                  int flags) {
  tjhandle handle = tjInitCompress();
  int ret = tjCompress2(handle, data, width, 0, height, pixelFormat, jpegBuf,
                        jpegSize, jpegSubsamp, jpegQual, flags);
  tjDestroy(handle);

  return 0;
}

Mp4toAvi::Mp4toAvi(/* args */) { m_pavi = nullptr; }

Mp4toAvi::~Mp4toAvi() {
  m_lock.lock();
  if (m_pavi) {
    AVI_close(m_pavi);
    m_pavi = nullptr;
  }
  m_lock.unlock();

  if (format_ctx_) {
    avformat_close_input(&format_ctx_);
    format_ctx_ = nullptr;
  }
}

bool Mp4toAvi::init(std::string strpath) {
  av_init_packet(&pkt_);
  video_file_name_ = strpath;
  // allocate format context
  format_ctx_ = avformat_alloc_context();
  if (format_ctx_ == nullptr) {
    return false;
  }

  format_ctx_->probesize = 100 * 1024 * 1024;  // 100MB
  format_ctx_->max_analyze_duration = 100 * AV_TIME_BASE; // 100s

  // trying to open the input file stream
  auto ret = avformat_open_input(&format_ctx_, strpath.c_str(), NULL, NULL);
  if (ret) {
    return false;
  }

  avformat_find_stream_info(format_ctx_, nullptr);
  int video_stream = -1;
  for (unsigned int i = 0; i < format_ctx_->nb_streams; i++) {
    if (format_ctx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      video_stream = i;
      break;
    }
  }

  if (video_stream == -1) {
    return false;
  }

  stream_index_ = video_stream;

  auto pCodecCtxOrig = format_ctx_->streams[stream_index_]->codecpar;
  int width = pCodecCtxOrig->width;
  int height = pCodecCtxOrig->height;
  m_ffmpeg.SetAvcodecParameters(pCodecCtxOrig);
  if (!m_ffmpeg.Init(width, height)) {
    return false;
  }
  std::string oldstr = ".mp4";
  std::string newstr = ".avi";
  std::string stravipath = StringUtil::ReplaceAll(strpath, oldstr, newstr);
  auto avihand = AVI_open_output_file(stravipath.c_str());
  if (!avihand) {
    return false;
  }
  char str[] = "mjpg";
  AVI_set_video(avihand, width, height, 30, str);
  m_pavi = avihand;

  return true;
}

void Mp4toAvi::run() {
  std::shared_ptr<unsigned char[]> data2(
      new unsigned char[width_ * height_ * 3]);
  std::shared_ptr<void> deferFunc(nullptr, [this](auto p) {
    m_lock.unlock();
  });
  m_lock.lock();
  int index = 0;
  while (true) {
    auto read_res = av_read_frame(format_ctx_, &pkt_);
    if (read_res < 0) {
      break;
    }
    if (pkt_.stream_index == stream_index_) 
    {
      
      auto frame_id = pkt_.pts / 512;
      int outid = 0;
      int outsize = 0;
      uint8_t *pdata = m_ffmpeg.Decode(0, pkt_.data, pkt_.size, outid, outsize);
      if (pdata && outsize > 0) {

        size_t jpegsize = 0;
        auto p = data2.get();
        int width = m_ffmpeg.GetDecodeWidth();
        int height = m_ffmpeg.GetDecodeHeight();
        // std::shared_ptr<unsigned char[]> prgb(new unsigned char[outsize]);
        // memcpy(prgb.get(),pdata,outsize);
        // RunAddTaskQueue(video_file_name_, [this,prgb,width, height, data2,deferFunc] {

        //   jpeg_compress(prgb.get(), width, height, TJPF_BGR, &p, &jpegsize,
        //                 TJSAMP_420, 30, 0);
        //   if (jpegsize > 0) {
        //     AVI_write_frame(m_pavi, (char *)p, jpegsize, 1);
        //   }
        // });
        //AVI_set_video_position((avi_t *)m_pavi, frame_id);
        jpeg_compress(pdata, width, height, TJPF_BGR, &p, &jpegsize,
        TJSAMP_420,30, 0);
        if (jpegsize > 0) {
        
          AVI_write_frame(m_pavi, (char *)p, jpegsize, 1);
        }
      }
      av_packet_unref(&pkt_);
    }
  }
}

