/*
 * @Author: huang123aini aiya123aini@163.com
 * @Date: 2023-07-04 13:33:03
 * @LastEditors: huang123aini aiya123aini@163.com
 * @LastEditTime: 2024-05-29 18:25:46
 * @FilePath: /data-engine/modules/video/image_converter.cxx
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */

#include "image_converter.h"
#include <cstring>
#include <vector>

#include "libyuv.h"
extern "C" {
#include "libavutil/pixfmt.h"
}

#include "common/easylogging++.h"
#include "common/image_data.h"

namespace {
std::vector<char> yuv422toyuv420(std::vector<char> &&yuv422_buffer, int width,
                                 int height) {
  std::vector<char> yuv420_buffer;
  yuv420_buffer.resize(width * height * 3 / 2);
  uint8_t *in = (uint8_t *)yuv422_buffer.data();
  uint8_t *out = (uint8_t *)yuv420_buffer.data();

  /*copy y data*/
  memcpy(out, in, width * height);

  int w = 0, h = 0;

  uint8_t *pu = out + (width * height);
  uint8_t *inu1 = in + (width * height);
  uint8_t *inu2 = inu1 + (width / 2);

  uint8_t *pv = pu + ((width * height) / 4);
  uint8_t *inv1 = inu1 + ((width * height) / 2);
  uint8_t *inv2 = inv1 + (width / 2);

  for (h = 0; h < height; h += 2) {
    inu2 = inu1 + (width / 2);
    inv2 = inv1 + (width / 2);
    for (w = 0; w < width / 2; w++) {
      *pu++ = ((*inu1++) + (*inu2++)) / 2; // average u sample
      *pv++ = ((*inv1++) + (*inv2++)) / 2; // average v samples
    }
    inu1 = inu2;
    inv1 = inv2;
  }

  return yuv420_buffer;
}

std::vector<char> nv12toyuv420(std::vector<char> &&nv12_buffer, int width,
                               int height) {
  std::vector<char> yuv420_buffer;
  yuv420_buffer.resize(nv12_buffer.size());

  uint8_t *in = (uint8_t *)nv12_buffer.data();
  uint8_t *out = (uint8_t *)yuv420_buffer.data();

  libyuv::NV12ToI420(&in[0], width, &in[width * height], width, &out[0], width,
                     &out[width * height], width / 2,
                     &out[width * height * 5 / 4], width / 2, width, height);

  return yuv420_buffer;
}
} // namespace

ImageData ImageConverter::convertToYuv420(ImageData &&image) {
  // The received image is raw yuv420p, do nothing.
  if (image.format == AV_PIX_FMT_YUV420P) {
    return image;
  }

  // In case the received format is nv12, need convert it into yuv420
  if (image.format == AV_PIX_FMT_NV12) {
    ImageData yuv420_image;
    yuv420_image.simpleCopy(image);
    yuv420_image.data =
        nv12toyuv420(std::move(image.data), image.width, image.height);
    yuv420_image.format = AV_PIX_FMT_YUV420P;

    return yuv420_image;
  }

  if (image.format == AV_PIX_FMT_YUVJ422P) {
    ImageData yuv420_image;
    yuv420_image.simpleCopy(image);
    yuv420_image.data =
        yuv422toyuv420(std::move(image.data), image.width, image.height);
    yuv420_image.format = AV_PIX_FMT_YUV420P;

    return yuv420_image;
  }

  LOG(ERROR) << "Receive unknown format!" << image.format;
  return ImageData();
}

