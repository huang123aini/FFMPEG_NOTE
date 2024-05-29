

#include "videoconverter.h"

#include <common/easylogging++.h>
#include <video_muxer.h>
#include <avilib.h>
#include <turbojpeg.h>
#include <jpeglib.h>
#include <libyuv.h>

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/avutil.h"
#include "libavutil/hwcontext.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/log.h"
#include <libswscale/swscale.h>
}


class AviWriter {
 public:
  AviWriter(const std::string &file, int width, int height);
  ~AviWriter();
  void WriteFrame(std::vector<char> &data);
  void WriteFrame(char *data, size_t size);
  operator bool() const;

 private:
  avi_t *handle_ = nullptr;
};

AviWriter::AviWriter(const std::string &file, int width, int height) {
  handle_ = AVI_open_output_file(file.data());
  if (handle_) {
    char buf[] = "mjpg";
    AVI_set_video(handle_, width, height, 30, buf);
  }
}

AviWriter::~AviWriter() {
  if (handle_) AVI_close(handle_);
  handle_ = nullptr;
}

void AviWriter::WriteFrame(std::vector<char> &data) {
  if (handle_) {
    AVI_write_frame(handle_, data.data(), data.size(), 1);
  }
}

void AviWriter::WriteFrame(char *data, size_t size) {
  if (handle_) {
    AVI_write_frame(handle_, data, size, 1);
  }
}

AviWriter::operator bool() const { return handle_ != nullptr; }

VideoConverter::VideoConverter() {
#ifdef NDEBUG
  av_log_set_level(AV_LOG_QUIET);
#endif
}

VideoConverter::~VideoConverter() { Close(); }

void VideoConverter::SetImageSize(int width, int height) {
  width_ = width;
  height_ = height;
}

void VideoConverter::SetCodecParmeters(AVCodecParameters *parameter) {
  if (parameters_) {
    avcodec_parameters_free(&parameters_);
  }
  parameters_ = avcodec_parameters_alloc();
  avcodec_parameters_copy(parameters_, parameter);
}

bool VideoConverter::Convert(int id, std::vector<char> &&data, bool remain) {
  if (!InitFFmpeg()) return false;
  if (remain) {
    remain_ids_.insert(id);
  }

  int ret = 0;
  AVPacket pkt;
  av_init_packet(&pkt);
  pkt.size = data.size();
  pkt.data = (uint8_t *)av_malloc(data.size());
  pkt.pts = id;
  memcpy(pkt.data, &data[0], data.size());

  ret = av_packet_from_data(&pkt, pkt.data, pkt.size);
  if (ret == 0) Decode(&pkt);

  av_packet_unref(&pkt);
  av_free(pkt.data);
  pkt.data = nullptr;
  pkt.size = -1;
  return true;
}

void VideoConverter::Close() {
  // flush
  Decode(nullptr);
  Encode(nullptr);
  DestoryFFmpeg();
}

bool VideoConverter::InitFFmpeg() {
  if (initialized_) return true;
  if (!to_avi_ && !to_mp4_ && !to_image_) {
    return false;
  }
  sw_format_ = AV_PIX_FMT_YUV420P;
  hw_format_ = AV_PIX_FMT_NONE;

  if (to_mp4_) {
    muxer_ = new VideoMuxer;
    muxer_->setCodecId(AV_CODEC_ID_H265);
    if (!muxer_->init(mp4_file_, width_, height_)) {
      DestoryFFmpeg();
      return false;
    }
  }

  frame_ = av_frame_alloc();
  sw_frame_ = av_frame_alloc();
  grey_frame_ = av_frame_alloc();
  if (!frame_ || !sw_frame_ || !grey_frame_) {
    DestoryFFmpeg();
    return false;
  }

  CreateGreyFrame();

  auto error_handle = [this](const char *func, int errcode) {
    char str[128] = {0};
    DestoryFFmpeg();
    av_strerror(errcode, str, sizeof(str));
    LOG(ERROR) << func << " failed:" << str;
  };

  // decode
  auto decodec = avcodec_find_decoder(AV_CODEC_ID_H265);
  if (!decodec) {
    LOG(ERROR) << "could not find decoder";
    return false;
  }
  decodec_ctx_ = avcodec_alloc_context3(decodec);
  if (!decodec_ctx_) {
    LOG(ERROR) << "could not alloc codec context";
    return false;
  }
  decodec_ctx_->width = width_;
  decodec_ctx_->height = height_;
  decodec_ctx_->thread_count = 4;
  decodec_ctx_->pix_fmt = (AVPixelFormat)sw_format_;
#ifdef NDEBUG
  av_opt_set(decodec_ctx_->priv_data, "x265-params", "log-level=none", 0);
#endif

  for (int i = 0;; i++) {
    const AVCodecHWConfig *config = avcodec_get_hw_config(decodec, i);
    if (config == nullptr) break;
    if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) &&
        config->device_type == AV_HWDEVICE_TYPE_CUDA) {
      av_hwdevice_ctx_create(&hw_device_ctx_, AV_HWDEVICE_TYPE_CUDA, NULL, NULL,
                             0);
      if (hw_device_ctx_) {
        decodec_ctx_->hw_device_ctx = av_buffer_ref(hw_device_ctx_);
        if (decodec_ctx_->hw_device_ctx) hw_format_ = config->pix_fmt;
      }
    }
  }
  if (parameters_) {
    avcodec_parameters_to_context(decodec_ctx_, parameters_);
  }

  int ret = avcodec_open2(decodec_ctx_, decodec, NULL);
  if (ret < 0) {
    error_handle("avcodec_open2 decoder", ret);
    return false;
  }

  // encode
  auto encodec = avcodec_find_encoder_by_name("hevc_nvenc");
  encodec_ctx_ = avcodec_alloc_context3(encodec);
  if (!encodec_ctx_) {
    error_handle("avcodec_alloccontext3 encoder", 0);
    return false;
  }
  encodec_ctx_->width = decodec_ctx_->width;
  encodec_ctx_->height = decodec_ctx_->height;
  encodec_ctx_->time_base = AVRational{1, 30};
  encodec_ctx_->framerate = AVRational{30, 1};
  encodec_ctx_->gop_size = 10;
  encodec_ctx_->pix_fmt = (AVPixelFormat)sw_format_;
  encodec_ctx_->profile = FF_PROFILE_HEVC_MAIN;
  encodec_ctx_->codec_type = AVMEDIA_TYPE_VIDEO;
  AVDictionary *options = NULL;
  av_dict_set(&options, "preset", "ultrafast", 0);
  av_dict_set(&options, "tune", "zero-latency", 0);

  std::string params = "keyint=1:crf=18";
#ifdef NDEBUG
  params += "log-level=none";
#endif
  av_opt_set(encodec_ctx_->priv_data, "x265-params", params.data(), 0);

  if (to_avi_ || to_image_) {
    jpeg_handle_ = tjInitCompress();
    if (!jpeg_handle_) {
      DestoryFFmpeg();
      return false;
    }
    auto size = av_image_get_buffer_size(AV_PIX_FMT_RGB24, width_, height_, 1);
    rgb_buffer_.resize(size);
    jpeg_.resize(width_ * height_ * 3);
  }

  if (to_avi_) {
    avi_writer_ = new AviWriter(avi_file_, width_, height_);
    if (!avi_writer_) {
      DestoryFFmpeg();
      return false;
    }
  }
  initialized_ = true;
  return true;
}

void VideoConverter::DestoryFFmpeg() {
  if (muxer_) {
    muxer_->close();
    delete muxer_;
  }
  if (frame_) av_frame_free(&frame_);
  if (sw_frame_) av_frame_free(&sw_frame_);
  if (grey_frame_) av_frame_free(&grey_frame_);
  if (decodec_ctx_) avcodec_free_context(&decodec_ctx_);
  if (encodec_ctx_) avcodec_free_context(&encodec_ctx_);
  if (hw_device_ctx_) av_buffer_unref(&hw_device_ctx_);
  if (jpeg_handle_) tjDestroy(jpeg_handle_);
  if (avi_writer_) delete avi_writer_;
  frame_ = nullptr;
  sw_frame_ = nullptr;
  decodec_ctx_ = nullptr;
  encodec_ctx_ = nullptr;
  hw_device_ctx_ = nullptr;
  jpeg_handle_ = nullptr;
  avi_writer_ = nullptr;
  first_frameid_ = -1;
  muxer_ = nullptr;
}

void VideoConverter::Decode(AVPacket *pkt) {
  if (!decodec_ctx_) return;
  int ret = 0;
  std::string func;
  do {
    ret = avcodec_send_packet(decodec_ctx_, pkt);
    func = "avcodec_send_packet";
    if (ret < 0) break;
    while (1) {
      ret = avcodec_receive_frame(decodec_ctx_, frame_);
      func = "avcodec_receive_frame";
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        ret = 0;
        break;
      }
      if (ret < 0) break;
      bool is_hwframe = frame_->format == hw_format_;
      auto frame_id = frame_->pts;
      // open encoder
      if (to_mp4_ && !encoder_opened_) {
        if (is_hwframe) {
          encodec_ctx_->hw_frames_ctx =
              av_buffer_ref(decodec_ctx_->hw_frames_ctx);
          if (!encodec_ctx_->hw_frames_ctx) break;
        }
        encodec_ctx_->pix_fmt = (AVPixelFormat)frame_->format;
        ret = avcodec_open2(encodec_ctx_, encodec_ctx_->codec, NULL);
        func = "avcodec_open2 encoder";
        if (ret < 0) break;
        encoder_opened_ = true;
      }

      if (first_frameid_ == -1) {
        first_frameid_ = frame_id;
        FillDummyFrame(grey_frame_);
      }
      if (IsRemain(frame_id)) {
        WriteAviFile(frame_);
        Encode(frame_);
      }
    }
  } while (0);

  if (ret < 0) {
    char str[128];
    av_strerror(ret, str, sizeof(str));
    LOG(ERROR) << func << " failed:" << str;
  }
}

void VideoConverter::Encode(const AVFrame *frame) {
  if (!encodec_ctx_ || !to_mp4_) return;
  int ret = 0;
  std::string func;

  do {
    ret = avcodec_send_frame(encodec_ctx_, frame);
    func = "avcodec_send_frame";
    if (ret < 0) break;
    while (1) {
      AVPacket pkt;
      av_init_packet(&pkt);
      pkt.data = nullptr;
      pkt.size = 0;
      pkt.pts = 0;
      ret = avcodec_receive_packet(encodec_ctx_, &pkt);
      func = "avcodec_receive_packet";
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        ret = 0;
        break;
      }
      if (ret < 0) break;
      if (muxer_) {
        muxer_->writeFrame((unsigned char *)pkt.data, pkt.size);
      }
      av_packet_unref(&pkt);
    }
  } while (0);

  if (ret < 0) {
    char str[128];
    av_strerror(ret, str, sizeof(str));
    LOG(ERROR) << func << " failed:" << str;
  }
}

void VideoConverter::WriteAviFile(const AVFrame *frame) {
  if (!to_avi_ && !to_image_ && !avi_writer_) return;
  bool is_hwframe = (frame->format == hw_format_);
  if (is_hwframe) {
    if (av_hwframe_transfer_data(sw_frame_, frame, 0) < 0) return;
    libyuv::NV12ToRGB24(
        (const uint8_t *)sw_frame_->data[0], sw_frame_->linesize[0],
        (const uint8_t *)sw_frame_->data[1], sw_frame_->linesize[1],
        &rgb_buffer_[0], sw_frame_->width * 3, sw_frame_->width,
        sw_frame_->height);
  } else {
    if (frame->format == AV_PIX_FMT_YUV420P) {
      libyuv::I420ToRGB24((const uint8_t *)frame->data[0], frame->linesize[0],
                          (const uint8_t *)frame->data[1], frame->linesize[1],
                          (const uint8_t *)frame->data[2], frame->linesize[2],
                          &rgb_buffer_[0], frame->width * 3, frame->width,
                          frame->height);
    } else if (frame->format == AV_PIX_FMT_RGB24) {
      av_image_copy_to_buffer(&rgb_buffer_[0], rgb_buffer_.size(), frame->data,
                              frame->linesize, (AVPixelFormat)frame->format,
                              frame->width, frame->height, 1);
    }
  }

  unsigned long jpegsize = 0;
  auto output = &jpeg_[0];
  auto image_qual = to_image_ ? 100 : 30;

  tjCompress2(jpeg_handle_, &rgb_buffer_[0], width_, 0, height_, TJPF_BGR,
              &output, &jpegsize, TJSAMP_420, image_qual, 0);
  if (jpegsize > 0) {
    if (to_avi_ && avi_writer_)
      avi_writer_->WriteFrame((char *)output, jpegsize);
    if (to_image_) {
      auto iskey = frame->pict_type == AV_PICTURE_TYPE_I;
      auto file = img_dir_ + "/" + std::to_string(frame->pts) + "_" +
                  std::to_string(iskey) + ".jpg";
      std::ofstream ofs(file, std::ios_base::binary);
      if (ofs) ofs.write((const char *)output, jpegsize);
    }
  }
  delete output;
}

void VideoConverter::FillDummyFrame(const AVFrame *frame) {
  bool is_hwframe = frame_->format == hw_format_;
  for (auto it = remain_ids_.begin(); it != remain_ids_.end(); ++it) {
    if (*it < first_frameid_) {
      WriteAviFile(frame);
      if (is_hwframe) {
        auto hw_frame = av_frame_alloc();
        av_hwframe_get_buffer(decodec_ctx_->hw_frames_ctx, hw_frame, 0);
        av_hwframe_transfer_data(hw_frame, frame, 0);
        Encode(hw_frame);
        av_frame_free(&hw_frame);
      } else {
        Encode(frame);
      }
    }
  }
}

bool VideoConverter::IsRemain(int id) { return remain_ids_.count(id) > 0; }

void VideoConverter::CreateGreyFrame() {
  int ret = 0;
  AVFrame *frame_rgb = nullptr;
  do {
    frame_rgb = av_frame_alloc();
    std::vector<uint8_t> rgb_data(width_ * height_ * 3, 127);  // grey color
    frame_rgb->width = width_;
    frame_rgb->height = height_;
    frame_rgb->format = AV_PIX_FMT_RGB24;
    ret = av_frame_get_buffer(frame_rgb, 0);
    if (ret < 0) break;
    ret = av_image_fill_arrays(frame_rgb->data, frame_rgb->linesize,
                               rgb_data.data(), AV_PIX_FMT_RGB24, width_,
                               height_, 32);
    if (ret < 0) break;

    grey_frame_->width = width_;
    grey_frame_->height = height_;
    grey_frame_->format = AV_PIX_FMT_YUV420P;
    ret = av_image_alloc(grey_frame_->data, grey_frame_->linesize,
                         grey_frame_->width, grey_frame_->height,
                         AV_PIX_FMT_YUV420P, 1);
    if (ret < 0) break;
    auto sws_ctx =
        sws_getContext(frame_rgb->width, frame_rgb->height, AV_PIX_FMT_RGB24,
                       grey_frame_->width, grey_frame_->height,
                       AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);

    sws_scale(sws_ctx, (const uint8_t *const *)frame_rgb->data,
              frame_rgb->linesize, 0, frame_rgb->height, grey_frame_->data,
              grey_frame_->linesize);

    sws_freeContext(sws_ctx);
  } while (0);
  if (frame_rgb) av_frame_free(&frame_rgb);
}

