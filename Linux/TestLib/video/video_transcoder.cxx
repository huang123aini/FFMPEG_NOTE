#include "video_transcoder.h"

#include "common/easylogging++.h"
#include "common/image_data.h"

VideoTranscoder::VideoTranscoder() {
  conversion_ = sws_getContext(width_, height_, AV_PIX_FMT_YUV422P, width_,
                               height_, AV_PIX_FMT_YUV420P, SWS_BICUBIC,
                               nullptr, nullptr, nullptr);
  if (conversion_ == nullptr) {
    LOG(ERROR) << "sws get conversion context failed";
  }
}

VideoTranscoder::~VideoTranscoder() { stopTranscoder(); }

// pass the transfered frame into function
// write the frame data into file
int VideoTranscoder::encodeWrite(AVFrame *frame, ImageData &output) {
  int ret = 0;
  AVPacket enc_pkt;
  /* send the frame to the encoder */
  av_init_packet(&enc_pkt);
  enc_pkt.data = nullptr;
  enc_pkt.size = 0;
  if (frame != nullptr) {
    frame->pts = frame_num_;
  }

  ret = avcodec_send_frame(encoder_codec_ctx_, frame);
  if (ret < 0) {
    LOG(ERROR) << "Error sending a frame for encoding, Error code : " << ret;
    goto end;
  }

  while (1) {
    ret = avcodec_receive_packet(encoder_codec_ctx_, &enc_pkt);
    if (ret) {
      break;
    }

    enc_pkt.stream_index = 0;  // set it to video stream
    output.height = height_;
    output.width = width_;
    output.data.resize(enc_pkt.size);
    memcpy(output.data.data(), (char *)enc_pkt.data, enc_pkt.size);
  }
end:
  if (ret == AVERROR_EOF) return 0;
  ret = ((ret == AVERROR(EAGAIN)) ? 0 : -1);
  return ret;
}

// encode a single frame into file
// p_image_data is the actual BGR24 data
// frame_len is the uncompressed frame len
int VideoTranscoder::transcodeSingleFrame(unsigned char *p_image_data,
                                          int frame_len, ImageData &output) {
  if (p_image_data == nullptr || frame_len < 0) {
    LOG(ERROR) << "function input parameter invalid!!!";
    return -1;
  }
  int ret;
  if (packet == nullptr) {
    LOG(ERROR) << "packet ptr is null!";
    return -1;
  }

  packet->size = frame_len;
  packet->data = (uint8_t *)av_malloc(packet->size);
  std::memcpy(packet->data, p_image_data, frame_len);
  ret = av_packet_from_data(packet, packet->data, packet->size);
  if (ret < 0) {
    LOG(ERROR) << "av_packet_from_data error";
    av_free(packet->data);
    return ret;
  }

  ret = decEnc(packet, output);
  av_packet_unref(packet);
  if (ret < 0) {
    LOG(ERROR) << "transcode failed. ret =" << ret;
    return -1;
  }

  return 0;
}

int VideoTranscoder::decEnc(AVPacket *pkt, ImageData &output) {
  AVFrame *frame = nullptr;
  AVFrame *transffered_frame = nullptr;
  AVFrame *hw_frame = nullptr;
  int ret = 0;

  ret = avcodec_send_packet(decoder_codec_ctx_, pkt);
  if (ret < 0) {
    LOG(ERROR) << "Error during decoding. Error code:" << ret;
    return ret;
  }

  while (ret >= 0) {
    if (!(frame = av_frame_alloc())) return AVERROR(ENOMEM);

    ret = avcodec_receive_frame(decoder_codec_ctx_, frame);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      av_frame_free(&frame);
      return 0;
    } else if (ret < 0) {
      LOG(ERROR) << "Error while decoding. Error code:" << ret;
      goto fail;
    }
// LOG(ERROR) << "%s decode mjpeg got frame , width : %d , height :
// %d\r\n",__FUNCTION__, frame->width, frame->height);
#if 1
    transffered_frame = av_frame_alloc();
    if (transffered_frame == nullptr) {
      LOG(ERROR) << "%s allocate transffered frame failed.";
      ret = -1;
      goto fail;
    }

    ret = convertYuvj422pToYuv420p(frame, transffered_frame);
    if (ret < 0) {
      LOG(ERROR) << "%s convert yuvj422p to yuv420p failed.";
      // ret = -1;
      goto fail;
    }
#endif
    // LOG(ERROR) << "%s convert yuvj422p to yuv420p , width : %d , height :
    // %d\r\n",__FUNCTION__, transffered_frame->width,
    // transffered_frame->height);

    hw_frame = av_frame_alloc();
    if (hw_frame == nullptr) {
      LOG(ERROR) << "hardware frame allocated failed";
      // av_frame_free(&frame);
      ret = -1;
      goto fail;
    }

    ret = av_hwframe_get_buffer(encoder_codec_ctx_->hw_frames_ctx, hw_frame, 0);
    if (ret < 0) {
      LOG(ERROR) << "av hardware frame get buffer failed";
      goto fail;
    }

    if (!hw_frame->hw_frames_ctx) {
      LOG(ERROR) << "hw frame frames context is nullptr";
      ret = AVERROR(ENOMEM);
      goto fail;
    }
    // transfer software frame to hardware frame
    ret = av_hwframe_transfer_data(hw_frame, transffered_frame, 0);
    if (ret < 0) {
      LOG(ERROR) << "Error while transfering data to surface, Error code:"
                 << ret;
      goto fail;
    }

    if ((ret = encodeWrite(hw_frame, output)) < 0) {
      LOG(ERROR) << "Error during encoding and writing.";
    } else {
      frame_num_++;
    }

  fail:
    av_frame_free(&frame);
    av_frame_free(&transffered_frame);
    av_frame_free(&hw_frame);

    if (ret < 0) return ret;
  }
  return 0;
}

// stop the encode process
// 1. flush the encoder
// 2. put an end pattern to the end of file
// flush the encoder
void VideoTranscoder::stopTranscoder() {
  /* add sequence end code to have a real MPEG file */
  if (encoder_codec_ctx_) {
    avcodec_close(encoder_codec_ctx_);
    encoder_codec_ctx_ = nullptr;
  }

  if (decoder_codec_ctx_) {
    avcodec_close(decoder_codec_ctx_);
    decoder_codec_ctx_ = nullptr;
  }

  av_buffer_unref(&hw_device_ctx_);

  encoder_codec_ = nullptr;
  decoder_codec_ = nullptr;

  if (packet != nullptr) {
    av_free(packet);
    packet = nullptr;
  }
}

int VideoTranscoder::setHwframeCtx(AVCodecContext *ctx,
                                   AVBufferRef *hw_device_ctx) {
  AVBufferRef *hw_frames_ref;
  AVHWFramesContext *frames_ctx = nullptr;
  int err = 0;

  hw_frames_ref = av_hwframe_ctx_alloc(hw_device_ctx);
  if (hw_frames_ref == nullptr) {
    LOG(ERROR) << "%s failed to create CUDA frame context";
    return -1;
  }

  frames_ctx = (AVHWFramesContext *)(hw_frames_ref->data);
  frames_ctx->format = AV_PIX_FMT_CUDA;
  frames_ctx->sw_format = AV_PIX_FMT_YUV420P;
  frames_ctx->width = width_;
  frames_ctx->height = height_;
  frames_ctx->initial_pool_size = 20;
  err = av_hwframe_ctx_init(hw_frames_ref);
  if (err < 0) {
    LOG(ERROR) << "%s Failed to initialize CUDA frame context.";
    av_buffer_unref(&hw_frames_ref);
    return err;
  }

  ctx->hw_frames_ctx = av_buffer_ref(hw_frames_ref);
  if (!ctx->hw_frames_ctx) {
    LOG(ERROR) << "%s av buffer ref failed";
    err = AVERROR(ENOMEM);
  }

  av_buffer_unref(&hw_frames_ref);
  return err;
}

// try to open the video file to write the frames
bool VideoTranscoder::startTranscoder() {
  if (!initEncoderCodec()) {
    LOG(ERROR) << "Failed to init encoder codec!";
    return false;
  }

  LOG(ERROR) << "Start encoder successfully! ";
  if (!initDecoderCodec()) {
    LOG(ERROR) << "Failed to init decoder codec!";
    return false;
  }

  packet = av_packet_alloc();
  if (nullptr == packet) {
    LOG(ERROR) << "%s av packet alloc failed. ";
    return false;
  }

  LOG(ERROR) << "Start decoder successfully! ";

  return true;
}

bool VideoTranscoder::initDecoderCodec(void) {
  int err;
  // avcodec_register_all();
  // av_register_all();

  decoder_codec_ = avcodec_find_decoder(AV_CODEC_ID_MJPEG);
  if (decoder_codec_ == nullptr) {
    LOG(ERROR)
        << "Could not find required decode codec, codec id AV_CODEC_ID_MJPEG ";
    return false;
  }

  decoder_codec_ctx_ = avcodec_alloc_context3(decoder_codec_);
  if (decoder_codec_ctx_ == nullptr) {
    LOG(ERROR) << "decode codec alloc contex failed!!! ";
    return false;
  }

  err = avcodec_open2(decoder_codec_ctx_, decoder_codec_, nullptr);
  if (err < 0) {
    LOG(ERROR) << "Cannot open video decoder codec. Error code :" << err;
    return false;
  }
  LOG(ERROR) << "init decoder codec success";

  return true;
}

bool VideoTranscoder::initEncoderCodec() {
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

  encoder_codec_ = avcodec_find_encoder_by_name("h264_nvenc");
  if (encoder_codec_ == nullptr) {
    LOG(ERROR) << "Could not find required codec, encoder name: nvenc_h264";
    return false;
  }

  encoder_codec_ctx_ = avcodec_alloc_context3(encoder_codec_);
  encoder_codec_ctx_->width = width_;
  encoder_codec_ctx_->height = height_;
  encoder_codec_ctx_->time_base =
      (AVRational){1, 30};  // frame rate preset to 30 fps
  encoder_codec_ctx_->framerate = (AVRational){30, 1};
  encoder_codec_ctx_->sample_aspect_ratio = (AVRational){1, 1};
  encoder_codec_ctx_->gop_size = 10;  // added by jinzhongxi change gop size
  encoder_codec_ctx_->pix_fmt = AV_PIX_FMT_CUDA;  // set the pix format for cuda

  // set the hw_frames_ctx for encoder's AVCodecContext
  err = setHwframeCtx(encoder_codec_ctx_, hw_device_ctx_);
  if (err < 0) {
    LOG(ERROR) << "Failed to set hwframe context. err code : " << err;
    return false;
  }

  // av codec open with the selected context
  err = avcodec_open2(encoder_codec_ctx_, encoder_codec_, nullptr);
  if (err < 0) {
    LOG(ERROR) << "Cannot open video encoder codec. Error code :" << err;
    return false;
  }

  LOG(ERROR) << "Init encoder codec successfully!";

  return true;
}

void VideoTranscoder::setEncodeImageWidth(int width) { width_ = width; }

int VideoTranscoder::getEncodeImageWidth(void) { return width_; }

void VideoTranscoder::setEncodeImageHeight(int height) { height_ = height; }

int VideoTranscoder::getEncodeImageHeight(void) { return height_; }

int VideoTranscoder::convertYuvj422pToYuv420p(AVFrame *src_frame,
                                              AVFrame *dst_frame) {
  if (src_frame == nullptr || dst_frame == nullptr) {
    LOG(ERROR) << "%s input parameter invalid";
    return -1;
  }

  // uint8_t *dst_data[4];
  // int dst_linesize[4];
  // int ret;
  // int src_w = 1280;
  int src_h = 720;
  int dst_w = 1280, dst_h = 720;
  // enum AVPixelFormat dst_pix_fmt = AV_PIX_FMT_YUV420P;

  /* buffer is going to be written to rawvideo file, no alignment */
  dst_frame->width = dst_w;
  dst_frame->height = dst_h;
  dst_frame->format = AV_PIX_FMT_YUV420P;

  // av frame get buffer(transfered YUV420P frame)
  int err = av_frame_get_buffer(dst_frame, 32);
  if (err < 0) {
    LOG(ERROR) << "%s sw frame get buffer failed. error code :" << err;
    return -1;
  }

  sws_scale(conversion_, (const uint8_t *const *)src_frame->data,
            src_frame->linesize, 0, src_h, dst_frame->data,
            dst_frame->linesize);

  return 0;
}

