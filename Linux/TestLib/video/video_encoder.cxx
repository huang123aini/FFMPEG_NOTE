
#include "video_encoder.h"

#include "common/easylogging++.h"

bool VideoEncoder::init(int width, int height) {
  width_ = width;
  height_ = height;

  // avcodec_register_all();
  // av_register_all();  // av register all (how many times it can be called)
  //   allocate hard ware device context
  int err = av_hwdevice_ctx_create(&hw_device_ctx_, AV_HWDEVICE_TYPE_CUDA,
                                   nullptr, nullptr, 0);
  if (err < 0) {
    LOG(ERROR) << "Failed to create a av cuda hard ware device, Error code :"
               << err;
    return false;
  }

  codec_ = avcodec_find_encoder_by_name("h264_nvenc");
  if (codec_ == nullptr) {
    LOG(ERROR) << "Could not find required codec, encoder name: nvenc_h264";
    return false;
  }

  codec_context_ = avcodec_alloc_context3(codec_);
  codec_context_->width = width_;
  codec_context_->height = height_;
  codec_context_->time_base =
      (AVRational){1, 30};  // frame rate preset to 30 fps
  codec_context_->framerate = (AVRational){30, 1};
  codec_context_->sample_aspect_ratio = (AVRational){1, 1};
  codec_context_->gop_size = 10;  // added by jinzhongxi change gop size
  codec_context_->pix_fmt = AV_PIX_FMT_CUDA;  // set the pix format for cuda

  if (setHwFrameCtx() < 0) {
    LOG(ERROR) << "Failed to set the hwframe context!";
    return false;
  }

  err = avcodec_open2(codec_context_, codec_, nullptr);
  if (err < 0) {
    LOG(ERROR) << "Cannot open video encoder codec. Error code :" << err;
    return false;
  }

  LOG(ERROR) << "Init encoder codec successfully!";
  return true;
}

bool VideoEncoder::encode(std::vector<char> &&input, std::vector<char> &output,
                          AVPixelFormat format) {
  auto frame = av_frame_alloc();
  frame->width = width_;
  frame->height = height_;
  frame->format = format;

  auto hwframe = av_frame_alloc();

  AVPacket enc_pkt;
  av_init_packet(&enc_pkt);
  enc_pkt.data = nullptr;
  enc_pkt.size = 0;

  std::shared_ptr<void> deferFunc(nullptr,
                                  [&frame, &enc_pkt, &hwframe](auto p) {
                                    av_frame_free(&frame);
                                    av_frame_free(&hwframe);
                                    av_packet_unref(&enc_pkt);
                                  });

  auto ret = av_frame_get_buffer(frame, 0);
  if (ret < 0) {
    LOG(ERROR) << "Failed to get the frame buffer!ret:" << ret;
    return false;
  }
  ret = av_image_fill_arrays(frame->data, frame->linesize,
                             (uint8_t *)input.data(), format, width_, height_,
                             32);
  if (ret < 0) {
    LOG(ERROR) << "Failed to fill arrays!ret:" << ret << " format:" << format;
    return false;
  }

  ret = av_hwframe_get_buffer(codec_context_->hw_frames_ctx, hwframe, 0);
  if (ret < 0) {
    LOG(ERROR) << "Failed to get the frame buffer!ret:" << ret;
    return false;
  }
  if (!hwframe->hw_frames_ctx) {
    LOG(ERROR) << "The hw_frames_ctx is nullptr!ret:" << ret;
    return false;
  }

  ret = av_hwframe_transfer_data(hwframe, frame, 0);
  if (ret < 0) {
    LOG(ERROR) << "The av_hwframe_transfer_data failed!ret:" << ret;
    return false;
  }

  ret = avcodec_send_frame(codec_context_, hwframe);
  if (ret < 0) {
    LOG(ERROR) << "Failed to send frame!ret:" << ret;
    return false;
  }

  ret = avcodec_receive_packet(codec_context_, &enc_pkt);
  if (ret < 0) {
    LOG(ERROR) << "Failed to receive packet!ret:" << ret;
    return false;
  }

  output.resize(enc_pkt.size);
  memcpy(output.data(), enc_pkt.data, enc_pkt.size);

  return true;
}

int VideoEncoder::setHwFrameCtx() {
  auto hw_frames_ref = av_hwframe_ctx_alloc(hw_device_ctx_);
  if (hw_frames_ref == nullptr) {
    LOG(ERROR) << "Failed to create CUDA frame context";
    return -1;
  }

  auto frames_ctx = (AVHWFramesContext *)(hw_frames_ref->data);
  frames_ctx->format = AV_PIX_FMT_CUDA;
  frames_ctx->sw_format = AV_PIX_FMT_NV12;
  frames_ctx->width = width_;
  frames_ctx->height = height_;
  frames_ctx->initial_pool_size = 20;
  auto ret = av_hwframe_ctx_init(hw_frames_ref);
  if (ret < 0) {
    LOG(ERROR) << "%s Failed to initialize CUDA frame context.";
    av_buffer_unref(&hw_frames_ref);
    return ret;
  }

  codec_context_->hw_frames_ctx = av_buffer_ref(hw_frames_ref);
  if (!codec_context_->hw_frames_ctx) {
    LOG(ERROR) << "%s av buffer ref failed";
    ret = AVERROR(ENOMEM);
  }

  av_buffer_unref(&hw_frames_ref);
  return ret;
}

