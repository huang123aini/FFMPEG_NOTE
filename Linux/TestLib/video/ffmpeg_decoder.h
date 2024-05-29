#ifndef FFMPEGDECODER_H
#define FFMPEGDECODER_H
#include <atomic>
#include <condition_variable>
#include <functional>
#include <list>
#include <map>
#include <mutex>
#include <stdint.h>
#include <thread>
struct AVCodec;
struct AVCodecContext;
struct AVCodecParserContext;
struct AVFrame;
struct SwsContext;
struct AVCodecParameters;


class CudaH265Decode;
class FFmpegDecoder {
public:
  // h265 or h264
  FFmpegDecoder();
  ~FFmpegDecoder();

  bool Init(int width, int height, bool bH265 = true);
  uint8_t *Decode(int dataID, uint8_t *pInData, int dataSize, int &outDataID,
                  int &outSize);

  void SetAvcodecParameters(void *p);
  void StopDecode();

  int GetDecodeWidth(){ return m_decodewidth; };
  int GetDecodeHeight(){ return m_decodeheight; };

  int GetWidth() { return m_width; };
  int GetHeight() { return m_height; };

private:
  AVCodec *m_pCodec;
  AVCodecContext *m_pCodecContext = nullptr;
  AVCodecParserContext *codec_parser;
  SwsContext *m_pSwsContext = nullptr;
  AVFrame *m_pFrameRGB = nullptr;
  AVFrame *m_pFrame = nullptr;
  uint8_t *m_pRGBBuffer = nullptr;
  AVCodecParameters *codecpar_ = nullptr;
  int m_pRgbSize;
  int m_width;
  int m_height;
  int m_decodewidth;
  int m_decodeheight;
  int m_hwfmt;
  bool is_stop;
};

class DecodeSemaphore {

public:
  DecodeSemaphore(long count = 0) : count(count) {}
  void wait() {

    std::unique_lock<std::mutex> lock(mx);
    cond.wait(lock, [&]() { return count > 0; });
    --count;
  }
  void waitTime() {

    std::unique_lock<std::mutex> lock(mx);
    cond.wait_for(lock,std::chrono::milliseconds(100), [&]() { return count > 0; });
    --count;
  }
  void signal() {

    std::unique_lock<std::mutex> lock(mx);
    ++count;
    cond.notify_one();
  }

  void reset(){
    count = 0;
  }

private:
  std::mutex mx;
  std::condition_variable cond;
  long count;
};

class AsyncVideoDecoder {
private:
  struct Data {
    Data() {
      pData = nullptr;
      size = 0;
      dataID = 0;
    }
    ~Data() {
      if (pData) {
        free(pData);
        pData = nullptr;
      }
    }
    uint8_t *pData;
    int size;
    int dataID;
  };

public:
  typedef std::function<void(uint8_t *pData, int dataSize, int dataID,
                             void *pExdata)>
      CallFuncEx;
  AsyncVideoDecoder(bool bCache = false);
  ~AsyncVideoDecoder();

  bool Init(int width, int height, bool bH265 = true);
  void AddDecodeData(uint8_t *pData, int dataSize, int dataID,
                     void *pExData = nullptr);
  void StopDecode();
  void SetCallFunc(
      std::function<void(uint8_t *pData, int dataSize, int dataID)> call) {
    m_call = call;
  }
  void SetCallFuncEx(std::function<void(uint8_t *pData, int dataSize,
                                        int dataID, void *pExdata)>
                         call) {
    m_callEx = call;
  }

  void AddCallFuncEx(std::string strName, CallFuncEx func);
  bool CheckHaveFunc(std::string strName);

  void Clear();
  // if dataid=-1 return the Latest data
  std::shared_ptr<AsyncVideoDecoder::Data> GetAndClearOldCache(int dataID = -1);

private:
  void OnDecode();
  std::function<void(uint8_t *pData, int dataSize, int dataID)> m_call;
  std::function<void(uint8_t *pData, int dataSize, int dataID, void *pExdata)>
      m_callEx;

  FFmpegDecoder *m_ffmpeg;
  std::list<Data *> m_listData;
  std::map<int, std::shared_ptr<Data>> m_mapCache;
  std::map<int, void *> m_mapExdata;
  std::map<std::string, CallFuncEx> m_mapcallfunc;

  DecodeSemaphore m_semaphore;
  std::mutex m_lockInData;
  std::mutex m_lockMap;
  bool m_bCache;
  bool m_bRun;
  bool m_bClear = false;
  bool m_bThreadStarted = false;
  std::thread m_thread;
};

class VideoDecoderManager {
public:
  static VideoDecoderManager *GetInstance();

  void Destroy();
  void AddDecoder(int decoderID, std::shared_ptr<AsyncVideoDecoder> pDecoder);

  std::shared_ptr<AsyncVideoDecoder> FindDecoder(int decoderID);
  std::map<int, std::shared_ptr<AsyncVideoDecoder>> *GetMap() {
    return &m_mapDecoder;
  }

  std::map<int, std::shared_ptr<AsyncVideoDecoder>> m_mapDecoder;
};
#endif
