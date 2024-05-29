
#include "video_decoder.h"

#include "assert.h"
#include "common/easylogging++.h"
#include "libyuv.h"

VideoDecoder::~VideoDecoder() {
  if (codec_context_) {
    avcodec_close(codec_context_);
  }

  if (codec_context_) {
    free(codec_context_);
  }

  if (picture_) {
    av_frame_free(&picture_);
  }
}
int VideoDecoder::init(int width, int height, AVCodecID codec_id) {
  
  codec_ = avcodec_find_decoder(codec_id);
  if (!codec_) {
    LOG(ERROR) << "V4L2_CORE: decoder is not found!codec_id:" << codec_id;
    return -1;
  }

  codec_context_ = avcodec_alloc_context3(codec_);
  if (codec_context_ == NULL) {
    LOG(ERROR) << "V4L2_CORE: FATAL memory allocation failure" << errno;
    return -1;
  }

  auto par = av_parser_init(codec_->id);
  if (par == nullptr) {
    LOG(ERROR) << "av_parser_init failed!";
    return -1;
  }

  for (int i = 0;; i++) {
    const AVCodecHWConfig *config = avcodec_get_hw_config(codec_, i);
    if (config == nullptr) {
      LOG(ERROR) << codec_->name << " not support "
                 << av_hwdevice_get_type_name(hw_type_);
      break;
    }
    LOG(INFO) << "avcodec get hardware config successfully!";
    LOG(INFO) << "index: " << i << " Codec name: " << codec_->name
              << " long name:" << codec_->long_name
              << " medis type:" << codec_->type << " codec id:" << codec_->id;

    if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) &&
        config->device_type == hw_type_) {
      hwfmt_ = config->pix_fmt;
      AVBufferRef *hwbufref = nullptr;
      auto ret =
          av_hwdevice_ctx_create(&hwbufref, hw_type_, nullptr, nullptr, 0);
      if (ret < 0) {
        LOG(ERROR) << "av hardware device context create failed. ret:" << ret;
        break;
      }

      codec_context_->hw_device_ctx = av_buffer_ref(hwbufref);
      if (codec_context_->hw_device_ctx == nullptr) {
        LOG(ERROR) << "av buffer ref failed.";
        break;
      }

      av_buffer_unref(&hwbufref);
    }
  }

  codec_context_->flags2 |= AV_CODEC_FLAG2_FAST;
  codec_context_->pix_fmt = AV_PIX_FMT_YUV420P;
  codec_context_->width = width;
  codec_context_->height = height;

  if (avcodec_open2(codec_context_, codec_, NULL) < 0) {
    LOG(ERROR) << "V4L2_CORE: (H264 decoder) couldn't open codec";
    avcodec_close(codec_context_);
    free(codec_context_);
    return -1;
  }

  picture_ = av_frame_alloc();

  width_ = width;
  height_ = height;

  LOG(INFO) << "The codec is initialized successfully!";

  return 0;
}

int VideoDecoder::decode(std::vector<char> &&input, std::vector<char> &output,
                         AVPixelFormat &output_format) {
  int got_frame = 0;
  LOG(DEBUG) << "Input h264 size: " << input.size();

  av_init_packet(&packet_);
  std::shared_ptr<void> defer_unref_packet(nullptr, [this](auto p) {
    av_packet_unref(&packet_);
    av_free(packet_.data);
  });
  packet_.size = input.size();
  packet_.data = (uint8_t *)av_malloc(input.size());
  std::memcpy(packet_.data, input.data(), input.size());
  auto ret = av_packet_from_data(&packet_, packet_.data, packet_.size);
  if (ret < 0) {
    LOG(ERROR) << "av_packet_from_data failed!";
    return -1;
  }

  AVFrame *hwframe = nullptr;
  std::shared_ptr<void> deferFunc(nullptr, [&hwframe](auto p) {
    if (hwframe) {
      av_frame_free(&hwframe);
    }
  });
  ret = libav_decode(codec_context_, picture_, hwframe, &got_frame, &packet_);

  if (ret < 0) {
    LOG(ERROR) << "V4L2_CORE: (H264 decoder) error while decoding frame";
    return ret;
  }

  if (got_frame) {
    auto real_frame = (hwframe == nullptr ? picture_ : hwframe);
    output_format = static_cast<AVPixelFormat>(real_frame->format);
    auto buffer_size =
        av_image_get_buffer_size(output_format, width_, height_, 1);
    if (buffer_size <= 0) {
      LOG(ERROR) << "Failed to get the buffer size for format:"
                 << real_frame->format << " width_:" << width_
                 << " height:" << height_;

      return -1;
    }
    if (decode_buffer_.size() != static_cast<std::size_t>(buffer_size)) {
      decode_buffer_.resize(buffer_size);
    }

    ret = av_image_copy_to_buffer(
        (uint8_t *)decode_buffer_.data(), buffer_size,
        (const unsigned char *const *)real_frame->data, real_frame->linesize,
        (AVPixelFormat)real_frame->format, codec_context_->width,
        codec_context_->height, 1);
    if (ret < 0) {
      LOG(ERROR) << "Failed to copy the image to buffer!size:" << buffer_size;
      return -1;
    }

    LOG(DEBUG) << "Image is decodec successfully!input:" << input.size()
               << " output:" << decode_buffer_.size();

    output = decode_buffer_;

    return ret;
  }

  LOG(ERROR) << "Failed to decode the input!";
  return -1;
}

int VideoDecoder::libav_decode(AVCodecContext *avctx, AVFrame *frame,
                               AVFrame *&hwframe, int *got_frame,
                               AVPacket *pkt) {
  int ret;

  *got_frame = 0;

  if (pkt) {
    ret = avcodec_send_packet(avctx, pkt);
    // In particular, we don't expect AVERROR(EAGAIN), because we read all
    // decoded frames with avcodec_receive_frame() until done.
    if (ret < 0) {
      LOG(ERROR) << "Failed to send packet!ret:" << ret;
      return ret;
    }
  }

  ret = avcodec_receive_frame(avctx, frame);
  if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) return ret;
  if (ret >= 0) *got_frame = 1;

  LOG(DEBUG) << "sw frame size:w:" << frame->width << " h:" << frame->height
             << " frame format:" << frame->format << " hwfmt_:" << hwfmt_;

  // hwfmt_ != AV_PIX_FMT_NONE indicates use hardware decode.
  if (frame->format == hwfmt_) {
    hwframe = av_frame_alloc();
    ret = av_hwframe_transfer_data(hwframe, frame, 0);
    if (ret < 0) {
      LOG(ERROR) << "hwdecode main image failed";
      return -1;
    }
    hwframe->pts = frame->pts;
    hwframe->pkt_dts = frame->pkt_dts;
    hwframe->pkt_duration = frame->pkt_duration;
    LOG(DEBUG) << "hw frame size:w:" << frame->width << " h:" << frame->height
               << "hwformat:" << hwframe->format
               << " swformat:" << frame->format
               << " 420format:" << AV_PIX_FMT_YUV420P;
  }

  return 0;
}

