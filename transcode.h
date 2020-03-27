extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswresample/swresample.h>
};
#include <memory>

class Transcode {
public:
  Transcode();
  ~Transcode();
  int ReadBuffer(char *file);
  int OpenOutput(char *filename);
  int Decode(int type, AVPacket *avpacket);
  int Decodec_init(int type);
  int init_resampler();
  int Encode(int type, AVFrame *decoded_frame);
  int CloseOutPut();
  int init_fifo();

private:
  AVCodecContext *dec_ctx_v_;
  AVCodecContext *dec_ctx_a_;

  AVCodecContext *enc_ctx_v;
  AVCodecContext *enc_ctx_a;

  AVFrame *decoded_frame_;
  AVFormatContext *ofmt_ctx_;
  AVPacket *encoded_pkt_;
  AVAudioFifo *fifo = NULL;
  SwrContext *resample_context = NULL;

  bool t1_u_;
  uint64_t t1_p_;

private:
};