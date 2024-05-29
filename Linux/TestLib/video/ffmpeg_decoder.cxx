#include "ffmpeg_decoder.h"

extern "C" {
#include <libavutil/avassert.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/avutil.h"
#include "libavutil/hwcontext.h"
#include "libavutil/imgutils.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libswresample/swresample.h"
#include "libswscale/swscale.h"
}

#include <malloc.h>
#include <thread>
#include "libyuv.h"

FFmpegDecoder::FFmpegDecoder() : is_stop(false) {

#ifdef NDEBUG
  av_log_set_level(AV_LOG_QUIET);
#endif
}

FFmpegDecoder::~FFmpegDecoder() {
  if (m_pCodecContext) {
    avcodec_close(m_pCodecContext);
    avcodec_free_context(&m_pCodecContext);
  }
  if (m_pFrameRGB) {
    av_frame_free(&m_pFrameRGB);
    m_pFrameRGB = nullptr;
  }
  if (m_pFrame) {
    // av_frame_free(&m_pFrame);
  }
  if (m_pRGBBuffer) {
    free(m_pRGBBuffer);
    m_pRGBBuffer = nullptr;
  }
  if (m_pSwsContext) {
    sws_freeContext(m_pSwsContext);
  }
}

void FFmpegDecoder::SetAvcodecParameters(void *p)
{
  codecpar_ = (AVCodecParameters*)p;
  //avcodec_parameters_to_context(m_pCodecContext, (const AVCodecParameters*)p);
}

bool FFmpegDecoder::Init(int width, int height, bool bH265) {
  m_width = width;
  m_height = height;

  AVCodecID codeid = AV_CODEC_ID_H265;
  if (bH265 == false) {
    codeid = AV_CODEC_ID_MJPEG;
  }
  auto pCodec = avcodec_find_decoder(codeid);
  if (!pCodec) {
    return false;
  }

  pCodec->capabilities |= AV_CODEC_CAP_DELAY;
  auto pCodecContext = avcodec_alloc_context3(pCodec);
  if (pCodecContext == NULL) {
    return false;
  }

  pCodecContext->thread_count = 4;
  // codec_context_->flags = codec_context_->flags & AVFMT_FLAG_NOBUFFER;
  // codec_context_->flags = codec_context_->flags & AV_CODEC_FLAG_LOW_DELAY;

  m_pCodec = pCodec;
  m_pCodecContext = pCodecContext;

  // cuda
  AVHWDeviceType hwType = AV_HWDEVICE_TYPE_CUDA;
  AVPixelFormat hwfmt = AV_PIX_FMT_NONE;
  m_hwfmt = AV_PIX_FMT_NONE;
  bool hwdevice = false;
  for (int i = 0;; i++) {
    const AVCodecHWConfig *config = avcodec_get_hw_config(pCodec, i);
    if (config == nullptr) {
      break;
    }

    if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) &&
        config->device_type == hwType) {
      hwfmt = config->pix_fmt;
      AVBufferRef *hwbufref = nullptr;
      auto ret = av_hwdevice_ctx_create(&hwbufref, hwType, nullptr, nullptr, 0);
      if (ret < 0) {
        break;
      }

      pCodecContext->hw_device_ctx = av_buffer_ref(hwbufref);

      if (pCodecContext->hw_device_ctx == nullptr) {
        break;
      }
      hwdevice = true;
      av_buffer_unref(&hwbufref);

      m_hwfmt = hwfmt;
    }
  }

  if(this->codecpar_){
    avcodec_parameters_to_context(pCodecContext, this->codecpar_);
  }

  if (avcodec_open2(pCodecContext, pCodec, NULL) < 0) {
    avcodec_close(pCodecContext);
    avcodec_free_context(&pCodecContext);
    return false;
  }
  // to rgb
  // int numBytes = avpicture_get_size(AV_PIX_FMT_RGB32, width, height);
  int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, width, height, 1);
  m_pRGBBuffer = (uint8_t *)av_malloc(numBytes * sizeof(uint8_t));
  m_pFrameRGB = av_frame_alloc();
  // m_pFrame = av_frame_alloc();
  /*
  avpicture_fill((AVPicture *)m_pFrameRGB, m_pRGBBuffer, AV_PIX_FMT_RGB32,
                 width, height);
  */
  av_image_fill_arrays(m_pFrameRGB->data, m_pFrameRGB->linesize, m_pRGBBuffer,
                       AV_PIX_FMT_RGB24, width, height, 1);
  if (hwdevice) {
    m_pSwsContext =
        sws_getContext(width, height, AV_PIX_FMT_NV12, width, height,
                       AV_PIX_FMT_RGB24, SWS_POINT, nullptr, nullptr, nullptr);
  } else {
    m_pSwsContext =
        sws_getContext(width, height, AV_PIX_FMT_YUV420P, width, height,
                       AV_PIX_FMT_RGB24, SWS_POINT, nullptr, nullptr, nullptr);
  }
  //
  m_pRgbSize = numBytes;

  return true;
}

void FFmpegDecoder::StopDecode() { is_stop = true; }

uint8_t *FFmpegDecoder::Decode(int dataID, uint8_t *pInData, int dataSize,
                               int &outDataID, int &outSize) {
  AVPacket packet;
  av_init_packet(&packet);
  packet.size = dataSize;
  packet.data = (uint8_t *)av_malloc(dataSize);
  memcpy(packet.data, pInData, dataSize);

  std::shared_ptr<void> deferFunc(nullptr, [&packet](auto p) {
    av_packet_unref(&packet);
    av_free(packet.data);
    packet.data = nullptr;
    packet.size = 0;
  });

  auto ret = av_packet_from_data(&packet, packet.data, packet.size);
  if (ret < 0) {
    return nullptr;
  }

  packet.pts = dataID;
  ret = avcodec_send_packet(m_pCodecContext, &packet);
  if (ret < 0) {
    return nullptr;
  }
  while (!is_stop) {
    AVFrame *pFrame = av_frame_alloc();
    AVFrame *pFrameCUDA = av_frame_alloc();

    std::shared_ptr<void> deferFunc(nullptr, [&pFrameCUDA, &pFrame](auto p) {
      av_frame_free(&pFrame);
      av_frame_free(&pFrameCUDA);
    });
    if (!pFrame) {
      break;
    }
    ret = avcodec_receive_frame(m_pCodecContext, pFrame);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      break;
      ;
    } else if (ret < 0) {
      break;
    }

    if (pFrame->format == (AVPixelFormat)m_hwfmt) {
      if ((ret = av_hwframe_transfer_data(pFrameCUDA, pFrame, 0)) < 0) {
        break;
      }
      m_pFrame = pFrameCUDA;
      
      libyuv::NV12ToRGB24(
          (const uint8_t *)pFrameCUDA->data[0], pFrameCUDA->linesize[0],
          (const uint8_t *)pFrameCUDA->data[1], pFrameCUDA->linesize[1],
          m_pRGBBuffer, pFrameCUDA->width * 3, pFrameCUDA->width,
          pFrameCUDA->height);

    } else {
      m_pFrame = pFrame;

      libyuv::I420ToRGB24((const uint8_t *)pFrame->data[0], pFrame->linesize[0],
                         (const uint8_t *)pFrame->data[1], pFrame->linesize[1],
                         (const uint8_t *)pFrame->data[2], pFrame->linesize[2],
                         m_pRGBBuffer, pFrame->width * 3, pFrame->width,
                         pFrame->height);
    }
    // sws_scale(m_pSwsContext, (uint8_t const *const *)m_pFrame->data,
    //           m_pFrame->linesize, 0, m_height, m_pFrameRGB->data,
    //           m_pFrameRGB->linesize);
    outDataID = pFrame->pts;
    outSize = m_pRgbSize;
    m_decodeheight = m_pFrame->height;
    m_decodewidth = m_pFrame->width;

    return m_pRGBBuffer;
  }

  return nullptr;
}

AsyncVideoDecoder::AsyncVideoDecoder(bool bCache)
    : m_call(nullptr),
      m_callEx(nullptr),
      m_ffmpeg(nullptr),
      m_bCache(bCache),
      m_bRun(false) {}

AsyncVideoDecoder::~AsyncVideoDecoder() {
  malloc_trim(0);
  if (m_bThreadStarted) {
    if (m_thread.joinable()) m_thread.join();
  }
  if (m_ffmpeg) {
    delete m_ffmpeg;
    m_ffmpeg = nullptr;
  }
}

bool AsyncVideoDecoder::Init(int width, int height, bool bH265) {
  m_ffmpeg = new FFmpegDecoder();
  m_ffmpeg->Init(width, height);
  m_bRun = true;
  m_bThreadStarted = true;
  m_thread = std::thread([this]() { OnDecode(); });
  return true;
}

void AsyncVideoDecoder::StopDecode() {
  m_bRun = false;
  m_semaphore.signal();
  if (m_ffmpeg) {
    m_ffmpeg->StopDecode();
  }
}

void AsyncVideoDecoder::AddCallFuncEx(std::string strName, CallFuncEx func) {
  m_mapcallfunc[strName] = func;
}

bool AsyncVideoDecoder::CheckHaveFunc(std::string strName) {
  auto find = m_mapcallfunc.find(strName);
  if (find != m_mapcallfunc.end()) {
    return true;
  }
  return false;
}

void AsyncVideoDecoder::AddDecodeData(uint8_t *pData, int dataSize, int dataID,
                                      void *pExData) {
  Data *pTempData = new Data;
  pTempData->pData = (uint8_t *)malloc(dataSize);
  memcpy(pTempData->pData, pData, dataSize);
  pTempData->size = dataSize;
  pTempData->dataID = dataID;

  m_lockInData.lock();
  m_listData.push_back(pTempData);

  if (pExData) {
    m_mapExdata[dataID] = pExData;
  }

  m_lockInData.unlock();

  m_semaphore.signal();
}

std::shared_ptr<AsyncVideoDecoder::Data> AsyncVideoDecoder::GetAndClearOldCache(
    int dataID) {
  std::unique_lock<std::mutex> lk(m_lockMap);
  if (dataID == -1) {
    if (m_mapCache.size()) {
      auto iter = m_mapCache.rbegin();
      m_mapCache.clear();
      return iter->second;
    }

  } else {
    auto findIter = m_mapCache.find(dataID);
    if (findIter != m_mapCache.end()) {
      m_mapCache.clear();
      return findIter->second;
    }
  }
  return nullptr;
}

void AsyncVideoDecoder::Clear() {
  m_bClear = true;
  std::list<Data *> listData;
  std::unique_lock<std::mutex> lk(m_lockInData);
  listData.swap(m_listData);
  for (auto &iter : listData) {
    delete iter;
  }
}

void AsyncVideoDecoder::OnDecode() {
  int outSize = 0;
  int outID = 0;
  while (m_bRun) {
    std::list<Data *> listData;
    std::unique_lock<std::mutex> lk(m_lockInData);
    listData.swap(m_listData);
    lk.unlock();

    if (listData.empty()) {
      m_semaphore.wait();
    }
    for (auto &iter : listData) {
      if (m_bClear) {
        delete iter;
        continue;
      }
      std::uint8_t *pData = nullptr;
      if (m_ffmpeg) {
        pData = m_ffmpeg->Decode(iter->dataID, iter->pData, iter->size, outID,
                                 outSize);
      }

      if (m_bCache && pData) {
        std::unique_lock<std::mutex> maplk(m_lockInData);
        std::shared_ptr<Data> pChar = std::make_shared<Data>();
        pChar->pData = (uint8_t *)malloc(outSize);
        memcpy(pChar->pData, pData, outSize);
        m_mapCache[outID] = pChar;
        maplk.unlock();
      }
      if (m_call && pData) {
        m_call(pData, outSize, outID);
      }
      if (pData) {
        void *pExData = nullptr;
        m_lockInData.lock();
        auto findData = m_mapExdata.find(outID);
        if (findData != m_mapExdata.end()) {
          pExData = findData->second;
          m_mapExdata.erase(findData);
        }
        m_lockInData.unlock();
        if (m_callEx) {
          m_callEx(pData, outSize, outID, pExData);
        }

        if (m_mapcallfunc.size()) {
          auto iter = m_mapcallfunc.begin();
          for (; iter != m_mapcallfunc.end(); iter++) {
            iter->second(pData, outSize, outID, pExData);
          }
        }
      }
      delete iter;
    }
    if (m_bClear) {
      m_bClear = false;
    }
  }
  {
    std::unique_lock<std::mutex> indatalk(m_lockInData);
    std::list<Data *>().swap(m_listData);
  }
}

VideoDecoderManager *VideoDecoderManager::GetInstance() {
  static VideoDecoderManager g_decodeManager;
  return &g_decodeManager;
}

void VideoDecoderManager::AddDecoder(
    int decoderID, std::shared_ptr<AsyncVideoDecoder> pDecoder) {
  m_mapDecoder[decoderID] = pDecoder;
}

void VideoDecoderManager::Destroy() {
  for (auto decoder : m_mapDecoder) {
    decoder.second->StopDecode();
  }
  m_mapDecoder.clear();
}

std::shared_ptr<AsyncVideoDecoder> VideoDecoderManager::FindDecoder(
    int decoderID) {
  auto find = m_mapDecoder.find(decoderID);
  if (find != m_mapDecoder.end()) {
    return find->second;
  }

  return nullptr;
}

