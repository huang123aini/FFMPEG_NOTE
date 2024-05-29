
#include "video_utils.h"
#include "common/easylogging++.h"
#include "common/image_data.h"
#include "common/image_distort_info.h"
#include "common/json.hpp"

extern "C" {
#include "libavutil/error.h"
}

namespace videoutils {
enum HEVCNALUnitType {
  HEVC_NAL_TRAIL_N = 0,
  HEVC_NAL_TRAIL_R = 1,
  HEVC_NAL_TSA_N = 2,
  HEVC_NAL_TSA_R = 3,
  HEVC_NAL_STSA_N = 4,
  HEVC_NAL_STSA_R = 5,
  HEVC_NAL_RADL_N = 6,
  HEVC_NAL_RADL_R = 7,
  HEVC_NAL_RASL_N = 8,
  HEVC_NAL_RASL_R = 9,
  HEVC_NAL_VCL_N10 = 10,
  HEVC_NAL_VCL_R11 = 11,
  HEVC_NAL_VCL_N12 = 12,
  HEVC_NAL_VCL_R13 = 13,
  HEVC_NAL_VCL_N14 = 14,
  HEVC_NAL_VCL_R15 = 15,
  HEVC_NAL_BLA_W_LP = 16,
  HEVC_NAL_BLA_W_RADL = 17,
  HEVC_NAL_BLA_N_LP = 18,
  HEVC_NAL_IDR_W_RADL = 19,
  HEVC_NAL_IDR_N_LP = 20,
  HEVC_NAL_CRA_NUT = 21,
  HEVC_NAL_IRAP_VCL22 = 22,
  HEVC_NAL_IRAP_VCL23 = 23,
  HEVC_NAL_RSV_VCL24 = 24,
  HEVC_NAL_RSV_VCL25 = 25,
  HEVC_NAL_RSV_VCL26 = 26,
  HEVC_NAL_RSV_VCL27 = 27,
  HEVC_NAL_RSV_VCL28 = 28,
  HEVC_NAL_RSV_VCL29 = 29,
  HEVC_NAL_RSV_VCL30 = 30,
  HEVC_NAL_RSV_VCL31 = 31,
  HEVC_NAL_VPS = 32,
  HEVC_NAL_SPS = 33,
  HEVC_NAL_PPS = 34,
  HEVC_NAL_AUD = 35,
  HEVC_NAL_EOS_NUT = 36,
  HEVC_NAL_EOB_NUT = 37,
  HEVC_NAL_FD_NUT = 38,
  HEVC_NAL_SEI_PREFIX = 39,
  HEVC_NAL_SEI_SUFFIX = 40,
};

bool isKeyFrame(unsigned char *pbuf, int buf_size) {
  unsigned int code = -1;
  int vps = 0, sps = 0, pps = 0, irap = 0;
  int i;

  for (i = 0; i < buf_size - 1; i++) {
    code = (code << 8) + pbuf[i];
    if ((code & 0xffffff00) == 0x100) {
      char nal2 = pbuf[i + 1];
      int type = (code & 0x7E) >> 1;

      if (code & 0x81) // forbidden and reserved zero bits
        return 0;

      if (nal2 & 0xf8) // reserved zero
        return 0;

      // printf("%s type : %d\r\n", __FUNCTION__, type);
      switch (type) {
      case HEVC_NAL_VPS:
        vps++;
        break;
      case HEVC_NAL_SPS:
        sps++;
        break;
      case HEVC_NAL_PPS:
        pps++;
        break;
      case HEVC_NAL_BLA_N_LP:
      case HEVC_NAL_BLA_W_LP:
      case HEVC_NAL_BLA_W_RADL:
      case HEVC_NAL_CRA_NUT:
      case HEVC_NAL_IDR_N_LP:
      case HEVC_NAL_IDR_W_RADL:
        irap++;
        break;
      }
    }
  }

  if (irap == 1) {
    return true;
  }

  return false;
}

std::string serializeDistortInfo(const ImageDistortInfo &info) {
  if (info.distort_type == -1) {
    LOG(INFO) << "Distort type is -1! no distort!";
    return "";
  }

  nlohmann::json j;
  j["internal_parameters"]["center_u"] = info.center_u;
  j["internal_parameters"]["center_v"] = info.center_v;
  j["internal_parameters"]["distort_param_data"] = info.distort_param_data;
  j["internal_parameters"]["distort_type"] = info.distort_type;
  j["internal_parameters"]["focal_u"] = info.focal_u;
  j["internal_parameters"]["focal_v"] = info.focal_v;

  return j.dump(4);
}

bool deserializeDistortInfo(const std::string &json_str,
                            ImageDistortInfo &info) {
  if (json_str.empty()) {
    return true;
  }

  ImageDistortInfo tmp;
  try {
    auto json = nlohmann::json::parse(json_str);
    json.at("internal_parameters").at("center_u").get_to(tmp.center_u);
    json.at("internal_parameters").at("center_v").get_to(tmp.center_v);
    json.at("internal_parameters")
        .at("distort_param_data")
        .get_to(tmp.distort_param_data);
    json.at("internal_parameters").at("distort_type").get_to(tmp.distort_type);
    json.at("internal_parameters").at("focal_u").get_to(tmp.focal_u);
    json.at("internal_parameters").at("focal_v").get_to(tmp.focal_v);
    tmp.distort_param_size = tmp.distort_param_data.size();
  } catch (std::exception &ex) {
    LOG(ERROR) << "Exception is caught when parsing " << json_str
               << "!ex:" << ex.what();
    return false;
  }

  info = tmp;

  return true;
}

} // namespace videoutils

std::string videoutils::printAvError(int ret) {
  char buff[128] = {0};
  av_strerror(ret, buff, 128);

  return std::string(buff);
}

