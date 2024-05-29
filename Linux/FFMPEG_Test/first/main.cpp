#include <iostream>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
}
#include <thread>
#include "Utils.h"

int main(int argc, char* argv[]) {
  std::unique_ptr<Utils> utils_ = std::make_unique<Utils>();

  AVFormatContext* av_format_context_ = NULL;
  AVPacket* pkt_ = NULL;
  AVCodecContext* codec_context_origin_ = NULL;
  AVCodecContext* codec_context_ = NULL;
  AVCodecParameters* av_codec_para_ = NULL;
  const AVCodec* codec_ = NULL;

  int video_stream_index_ = -1;
  int audio_stream_index_ = -1;

  std::string mediaFile = utils_->GetCurrentPath() + "../../640x360.mp4";

  /**
   * 打开文件
   */
  int ret =
      avformat_open_input(&av_format_context_, mediaFile.c_str(), NULL, NULL);
  if (ret < 0) {
    std::cout << "open avformat failed." << std::endl;
  }

  /**
   * 获取流信息
   */
  ret = avformat_find_stream_info(av_format_context_, NULL);
  if (ret < 0) {
    std::cout << "find stream info is failed." << std::endl;
  }

  for (int i = 0; i < av_format_context_->nb_streams; i++) {
    if (av_format_context_->streams[i]->codecpar->codec_type ==
        AVMEDIA_TYPE_VIDEO) {
      video_stream_index_ = i;
      break;
    }
  }

  if (video_stream_index_ == -1) {
    std::cout << "has no video stream" << std::endl;
  }

  /**
   * 打印输入和输出信息：长度、比特率、流格式etc.
   */
  av_dump_format(av_format_context_, 0, mediaFile.c_str(), 0);

  /**
   * 查找解码器
   */

  av_codec_para_ = av_format_context_->streams[video_stream_index_]->codecpar;
  codec_ = avcodec_find_decoder(av_codec_para_->codec_id);

  if (codec_ == NULL) {
    std::cout << "can't find codec." << std::endl;
  }

  /**
   * 根据解码器参数创建解码器内容
   */
  codec_context_origin_ = avcodec_alloc_context3(codec_);
  ret = avcodec_parameters_to_context(codec_context_origin_, av_codec_para_);
  if (codec_context_ == NULL) {
    std::cout << "can't alloc codec context" << std::endl;
  }

  /**
   * 打开解码器
   */

  codec_context_ = avcodec_alloc_context3(codec_);
  ret = avcodec_parameters_to_context(codec_context_, av_codec_para_);

  ret = avcodec_open2(codec_context_, codec_, NULL);
  if (ret < 0) {
    std::cout << "can't open decoder." << std::endl;
  }

  int frameCount = 0;
  /**
   * 分配packet
   */
  pkt_ = av_packet_alloc();
  while (av_read_frame(av_format_context_, pkt_) >= 0) {
    if (pkt_->stream_index == video_stream_index_) {
      frameCount++;
    }
    av_packet_unref(pkt_);
  }

  std::cout << "demux done. video frame count: " << frameCount << std::endl;

  av_packet_free(&pkt_);
  avcodec_close(codec_context_);
  avformat_close_input(&av_format_context_);
  avformat_free_context(av_format_context_);

  return 0;
}
