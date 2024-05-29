/*
 * @Author: huang123aini aiya123aini@163.com
 * @Date: 2024-04-25 17:46:06
 * @LastEditors: huang123aini aiya123aini@163.com
 * @LastEditTime: 2024-05-29 18:31:31
 * @FilePath: /data-engine/modules/video/videoconverter.h
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */

#ifndef __VIDEOCONVERTER_H__
#define __VIDEOCONVERTER_H__

#include <vector>
#include <string>
#include <set>

struct AVFrame;
struct AVPacket;
struct AVCodecContext;
struct AVBufferRef;
struct AVCodecParameters;

class VideoMuxer;
class AviWriter;

class VideoConverter {
 public:
  VideoConverter();
  ~VideoConverter();
  void EnableAvi(bool enable) { to_avi_ = enable; }
  void EnableMp4(bool enable) { to_mp4_ = enable; }
  void EnableImage(bool enable) { to_image_ = enable; }
  void SetMp4FileName(const std::string &name) { mp4_file_ = name; }
  void SetAviFileName(const std::string &name) { avi_file_ = name; }
  void SetImageSaveDir(const std::string &name) { img_dir_ = name; }
  std::string Mp4FileName() const { return mp4_file_; }
  std::string AviFileName() const { return avi_file_; }
  std::string ImageSaveDir() const { return img_dir_; }
  void SetImageSize(int width, int height);
  int Width() const { return width_; }
  int Height() const { return height_; }
  void SetCodecParmeters(AVCodecParameters *);

  bool Convert(int id, std::vector<char> &&data, bool remain = true);
  void Close();

 private:
  bool InitFFmpeg();
  void DestoryFFmpeg();
  void Decode(AVPacket *pkt);
  void Encode(const AVFrame *frame);
  void WriteAviFile(const AVFrame *frame);
  void FillDummyFrame(const AVFrame *frame);
  bool IsRemain(int id);
  void CreateGreyFrame();

 private:
  VideoMuxer *muxer_ = nullptr;
  AVCodecContext *decodec_ctx_ = nullptr;
  AVCodecContext *encodec_ctx_ = nullptr;
  AVCodecParameters *parameters_ = nullptr;
  int sw_format_ = 0;
  int hw_format_ = 0;
  AVBufferRef *hw_device_ctx_ = nullptr;
  AVFrame *frame_ = nullptr;
  AVFrame *sw_frame_ = nullptr;
  AVFrame *grey_frame_ = nullptr;

  std::string mp4_file_;
  std::string avi_file_;
  std::string img_dir_;

  AviWriter *avi_writer_ = nullptr;
  void *jpeg_handle_ = nullptr;
  std::vector<uint8_t> rgb_buffer_;
  std::vector<uint8_t> jpeg_;

  bool initialized_ = false;
  bool encoder_opened_ = false;
  bool to_avi_ = false;
  bool to_mp4_ = false;
  bool to_image_ = false;

  std::set<int32_t> remain_ids_;
  int first_frameid_ = -1;
  int width_ = 0;
  int height_ = 0;
};

#endif  // VIDEOCONVERT_H
