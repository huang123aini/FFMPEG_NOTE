
#include "video_muxer.h"

#include <iostream>

#include "common/easylogging++.h"
#include "common/elapsed_time_stat.h"
#include "error.h"
#include "video/video_utils.h"
extern "C" {
#include <libavutil/opt.h>
}

bool VideoMuxer::init(std::string filename, int width, int height) {
#ifdef NDEBUG
  av_log_set_level(AV_LOG_QUIET);
#endif
  width_ = width;
  height_ = height;
  filename_ = filename;

  avformat_alloc_output_context2(&output_format_context_, nullptr, nullptr,
                                 filename.c_str());
  if (output_format_context_ == nullptr) {
    LOG(ERROR) << "Failed to alloc the output format context!";
    return false;
  }

  if (!addStream()) {
    LOG(ERROR) << "Failed to add stream!";
    return false;
  }

  if (!openVideo()) {
    LOG(ERROR) << "Failed to open video!";
    return false;
  }

  //av_dump_format(output_format_context_, 0, filename.c_str(), 1);
  auto ret = avio_open(&output_format_context_->pb, filename.c_str(),
                       AVIO_FLAG_READ_WRITE);
  if (ret < 0) {
    LOG(ERROR) << "Couldnot open file " << filename << "!err:" << ret;
    return false;
  }

  ret = avformat_write_header(output_format_context_, nullptr);
  if (ret < 0) {
    LOG(ERROR) << "Failed to write header!err:" << ret;
    return false;
  }

  is_initialized_suc_ = true;

  LOG(ERROR) << "The muxer is initializer successfully!";
  return true;
}

VideoMuxer::~VideoMuxer() {
  LOG(INFO) << "Start deconstruct videomuxer";
  close();
}

void VideoMuxer::writeFrame(unsigned char *buffer, int size) {
  AVPacket packet;
  av_init_packet(&packet);
  packet.flags = videoutils::isKeyFrame(buffer, size) ? 1 : 0;
  packet.stream_index = out_stream_->index;
  packet.data = buffer;
  packet.size = size;
  pts_++;

  packet.pts = av_rescale_q_rnd(
      pts_, codec_context_->time_base, out_stream_->time_base,
      static_cast<AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
  packet.dts = av_rescale_q_rnd(
      pts_, codec_context_->time_base, out_stream_->time_base,
      static_cast<AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));

  packet.duration = 0;
  packet.pos = -1;

  {
    ElapsedTimeStat elapsed("av_interleaved_write_frame " + filename_,
                            50 * 1000);
    auto ret = av_interleaved_write_frame(output_format_context_, &packet);
    if (ret != 0) {
      LOG(ERROR) << "Failed to write the frame!";
    }
  }

  av_packet_unref(&packet);
}

bool VideoMuxer::addStream() {
  codec_ = avcodec_find_encoder(codec_id_);
  if (codec_ == nullptr) {
    LOG(ERROR) << "Failed to find the encoder for " << codec_id_;
    return false;
  }

  out_stream_ = avformat_new_stream(output_format_context_, nullptr);
  if (out_stream_ == nullptr) {
    LOG(ERROR) << "Failed to create new stream!";
    return false;
  }

  codec_context_ = avcodec_alloc_context3(codec_);
  if (codec_context_ == nullptr) {
    LOG(ERROR) << "Failed to alloc codec context!";
    return false;
  }

  codec_context_->codec_id = codec_id_;
  codec_context_->codec_type = AVMEDIA_TYPE_VIDEO;
  codec_context_->width = width_;
  codec_context_->height = height_;
  codec_context_->bit_rate = 400000;
  codec_context_->time_base = AVRational{1, 30};
  //  codec_context_->max_b_frames = 0;
  codec_context_->pix_fmt = AV_PIX_FMT_YUV420P;
  codec_context_->gop_size = 30;

  out_stream_->id = 0;
  out_stream_->time_base = codec_context_->time_base;
  if (codec_id_ == AV_CODEC_ID_H265) {
#ifdef NDEBUG
    av_opt_set(codec_context_->priv_data, "x265-params", "log-level=none", 0);
#endif
  }

  LOG(INFO) << "Add stream done!";
  return true;
}

bool VideoMuxer::openVideo() {
  auto ret = avcodec_open2(codec_context_, codec_, nullptr);
  if (ret < 0) {
    LOG(ERROR) << "Failed to open codec!ret:" << ret;
    return false;
  }

  ret = avcodec_parameters_from_context(out_stream_->codecpar, codec_context_);
  if (ret < 0) {
    LOG(ERROR)
        << "Failed to copy the parameter to stream from codec_context!ret:"
        << ret;
    return false;
  }

  LOG(INFO) << "Open the video successfully!";
  return true;
}

void VideoMuxer::setCodecId(AVCodecID codec_id) { codec_id_ = codec_id; }

void VideoMuxer::close() {
  if (output_format_context_ != nullptr && is_initialized_suc_) {
    av_write_trailer(output_format_context_);
  }

  if (codec_context_) {
    avcodec_free_context(&codec_context_);
    codec_context_ = nullptr;
  }

  if (output_format_context_ &&
      !(output_format_context_->oformat->flags & AVFMT_NOFILE)) {
    LOG(INFO) << "Close io context!";
    avio_closep(&output_format_context_->pb);
  }

  if (output_format_context_) {
    avformat_free_context(output_format_context_);
    output_format_context_ = nullptr;
  }
}

