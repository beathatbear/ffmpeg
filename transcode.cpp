#include "transcode.h"
extern "C" {
#include <libavutil/avassert.h>
#include <libavutil/common.h>
#include <libavutil/time.h>
#include <libavutil/timestamp.h>
};
Transcode::Transcode()
    : dec_ctx_v_(nullptr), dec_ctx_a_(nullptr), enc_ctx_v(nullptr),
      enc_ctx_a(nullptr), decoded_frame_(nullptr), ofmt_ctx_(nullptr),
      t1_u_(true), t1_p_(0) {
  decoded_frame_ = av_frame_alloc();
  encoded_pkt_ = av_packet_alloc();
}
Transcode::~Transcode() {
  if ((decoded_frame_) != NULL)
    av_frame_free(&decoded_frame_);
  if ((enc_ctx_v) != NULL)
    avcodec_free_context(&enc_ctx_v);
  if ((dec_ctx_v_) != NULL)
    avcodec_free_context(&dec_ctx_v_);
  if ((dec_ctx_a_) != NULL)
    avcodec_free_context(&dec_ctx_a_);
  if (encoded_pkt_ != NULL)
    av_packet_free(&encoded_pkt_);
}

int Transcode ::Decodec_init(int type) {
  const AVCodec *decodec_v, *decodec_a;
  decodec_v = avcodec_find_decoder(AV_CODEC_ID_H264);
  decodec_a = avcodec_find_decoder(AV_CODEC_ID_AAC);
  if (!decodec_a) {
    fprintf(stderr, "Codec not found %d \n", AV_CODEC_ID_AAC);
  }
  if (!decodec_v || !decodec_a) {
    fprintf(stderr, "Codec not found\n");
    exit(1);
  }
  dec_ctx_v_ = avcodec_alloc_context3(decodec_v);
  dec_ctx_a_ = avcodec_alloc_context3(decodec_a);
  dec_ctx_a_->channel_layout = AV_CH_LAYOUT_STEREO;
  dec_ctx_a_->sample_rate = 44100;
  dec_ctx_a_->sample_fmt = AV_SAMPLE_FMT_S16;
  // dec_ctx_a_->SamplesPerSecond
  if (avcodec_open2(dec_ctx_v_, decodec_v, NULL) < 0) {
    fprintf(stderr, "Could not open decodec_v\n");
  }
  if (avcodec_open2(dec_ctx_a_, decodec_a, NULL) < 0) {
    fprintf(stderr, "Could not open decodec_v\n");
  }
}

int Transcode ::init_fifo() {
  /* Create the FIFO buffer based on the specified output sample format. */
  if (!(fifo = av_audio_fifo_alloc(enc_ctx_a->sample_fmt, enc_ctx_a->channels,
                                   1))) {
    fprintf(stderr, "Could not allocate FIFO\n");
    return AVERROR(ENOMEM);
  }
  return 0;
}

int Transcode ::init_resampler() {
  int error;

  /*
   * Create a resampler context for the conversion.
   * Set the conversion parameters.
   * Default channel layouts based on the number of channels
   * are assumed for simplicity (they are sometimes not detected
   * properly by the demuxer and/or decoder).
   */
  resample_context = swr_alloc_set_opts(
      NULL, av_get_default_channel_layout(enc_ctx_a->channels),
      enc_ctx_a->sample_fmt, enc_ctx_a->sample_rate,
      av_get_default_channel_layout(dec_ctx_a_->channels),
      dec_ctx_a_->sample_fmt, dec_ctx_a_->sample_rate, 0, NULL);
  if (!resample_context) {
    fprintf(stderr, "Could not allocate resample context\n");
    return AVERROR(ENOMEM);
  }
  /*
   * Perform a sanity check so that the number of converted samples is
   * not greater than the number of samples to be converted.
   * If the sample rates differ, this case has to be handled differently
   */

  av_assert0(enc_ctx_a->sample_rate == dec_ctx_a_->sample_rate);

  /* Open the resampler with the specified parameters. */
  if ((error = swr_init(resample_context)) < 0) {
    fprintf(stderr, "Could not open resample context\n");
    swr_free(&resample_context);
    return error;
  }

  return 0;
}
static int init_converted_samples(uint8_t ***converted_input_samples,
                                  AVCodecContext *output_codec_context,
                                  int frame_size) {
  int error;

  /* Allocate as many pointers as there are audio channels.
   * Each pointer will later point to the audio samples of the corresponding
   * channels (although it may be NULL for interleaved formats).
   */
  if (!(*converted_input_samples =
            (uint8_t **)calloc(output_codec_context->channels,
                               sizeof(**converted_input_samples)))) {
    fprintf(stderr, "Could not allocate converted input sample pointers\n");
    return AVERROR(ENOMEM);
  }

  /* Allocate memory for the samples of all channels in one consecutive
   * block for convenience. */
  if ((error = av_samples_alloc(*converted_input_samples, NULL,
                                output_codec_context->channels, frame_size,
                                output_codec_context->sample_fmt, 0)) < 0) {
    // fprintf(stderr, "Could not allocate converted input samples (error
    // '%s')\n",
    //         av_err2str(error));
    av_freep(&(*converted_input_samples)[0]);
    free(*converted_input_samples);
    return error;
  }
  return 0;
}
static int convert_samples(const uint8_t **input_data, uint8_t **converted_data,
                           const int frame_size, SwrContext *resample_context) {
  int error;

  /* Convert the samples using the resampler. */
  if ((error = swr_convert(resample_context, converted_data, frame_size,
                           input_data, frame_size)) < 0) {
    // fprintf(stderr, "Could not convert input samples (error '%s')\n",
    //         av_err2str(error));
    return error;
  }

  return 0;
}
static int add_samples_to_fifo(AVAudioFifo *fifo,
                               uint8_t **converted_input_samples,
                               const int frame_size) {
  int error;

  /* Make the FIFO as large as it needs to be to hold both,
   * the old and the new samples. */
  if ((error = av_audio_fifo_realloc(fifo, av_audio_fifo_size(fifo) +
                                               frame_size)) < 0) {
    fprintf(stderr, "Could not reallocate FIFO\n");
    return error;
  }

  /* Store the new samples in the FIFO buffer. */
  if (av_audio_fifo_write(fifo, (void **)converted_input_samples, frame_size) <
      frame_size) {
    fprintf(stderr, "Could not write data to FIFO\n");
    return AVERROR_EXIT;
  }
  return 0;
}
static int init_output_frame(AVFrame **frame,
                             AVCodecContext *output_codec_context,
                             int frame_size) {
  int error;

  /* Create a new frame to store the audio samples. */
  if (!(*frame = av_frame_alloc())) {
    fprintf(stderr, "Could not allocate output frame\n");
    return AVERROR_EXIT;
  }

  (*frame)->nb_samples = frame_size;
  (*frame)->channel_layout = output_codec_context->channel_layout;
  (*frame)->format = output_codec_context->sample_fmt;
  (*frame)->sample_rate = output_codec_context->sample_rate;

  /* Allocate the samples of the created frame. This call will make
   * sure that the audio frame can hold as many samples as specified. */
  if ((error = av_frame_get_buffer(*frame, 0)) < 0) {
    // fprintf(stderr, "Could not allocate output frame samples (error '%s')\n",
    //         av_err2str(error));
    av_frame_free(frame);
    return error;
  }

  return 0;
}
static int64_t pts = 0;

int encode_audio_frame(AVFrame *frame, AVFormatContext *output_format_context,
                       AVCodecContext *output_codec_context,
                       int *data_present) {
  /* Packet used for temporary storage. */
  AVPacket output_packet;
  int error;
  av_init_packet(&output_packet);
  /* Set the packet data and size so that it is recognized as being empty. */
  output_packet.data = NULL;
  output_packet.size = 0;

  /* Set a timestamp based on the sample rate for the container. */
  if (frame) {
    frame->pts = pts;
    pts += frame->nb_samples;
  }

  /* Send the audio frame stored in the temporary packet to the encoder.
   * The output audio stream encoder is used to do this. */
  error = avcodec_send_frame(output_codec_context, frame);
  /* The encoder signals that it has nothing more to encode. */
  if (error == AVERROR_EOF) {
    error = 0;
    goto cleanup;
  } else if (error < 0) {
    // fprintf(stderr, "Could not send packet for encoding (error '%s')\n",
    //         av_err2str(error));
    return error;
  }

  /* Receive one encoded frame from the encoder. */
  error = avcodec_receive_packet(output_codec_context, &output_packet);
  /* If the encoder asks for more data to be able to provide an
   * encoded frame, return indicating that no data is present. */

  av_packet_rescale_ts(&output_packet, output_codec_context->time_base,
                       output_format_context->streams[1]->time_base);
  if (error == AVERROR(EAGAIN)) {
    error = 0;
    goto cleanup;
    /* If the last frame has been encoded, stop encoding. */
  } else if (error == AVERROR_EOF) {
    error = 0;
    goto cleanup;
  } else if (error < 0) {
    // fprintf(stderr, "Could not encode frame (error '%s')\n",
    // av_err2str(error));
    goto cleanup;
    /* Default case: Return encoded data. */
  } else {
    *data_present = 1;
  }
  /* Write one audio frame from the temporary packet to the output file. */
  // for (int i = 0; i < 16; i++) {
  //   printf("%02x ", output_packet.data[i]);
  // }
  output_packet.stream_index = 1;
  printf("stream index %d \n", output_packet.stream_index);
  if (*data_present &&
      (error = av_write_frame(output_format_context, &output_packet)) < 0) {
    // fprintf(stderr, "Could not write frame (error '%s')\n",
    // av_err2str(error));
    goto cleanup;
  }

cleanup:
  av_packet_unref(&output_packet);
  return error;
}
int Transcode ::Decode(int type, AVPacket *avpacket) {
  int ret, data_size;

  if (type == 0) {
    ret = avcodec_send_packet(dec_ctx_v_, avpacket);
    if (ret < 0) {
      fprintf(stderr, "Error submitting the packet to the decoder\n");
    }
    while (ret >= 0) {
      ret = avcodec_receive_frame(dec_ctx_v_, decoded_frame_);
      // printf("dec_ctx_v_ %d %d frame->format %d\n", decoded_frame_->width,
      //        decoded_frame_->height, decoded_frame_->pts);
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        return -1;
      else if (ret < 0) {
        fprintf(stderr, "Error during decoding\n");
      }
      data_size = av_get_bytes_per_sample(dec_ctx_v_->sample_fmt);
      if (data_size < 0) {
        /* This should not occur, checking just for paranoia */
        fprintf(stderr, "Failed to calculate data size\n");
      }
      Encode(0, decoded_frame_);
    }
  } else if (type == 1) {

    const int output_frame_size = enc_ctx_a->frame_size;
    printf("av_audio_fifo_size(fifo) %d %d\n", av_audio_fifo_size(fifo),
           output_frame_size);
    while (av_audio_fifo_size(fifo) < output_frame_size) {

      ret = avcodec_send_packet(dec_ctx_a_, avpacket);
      if (ret < 0) {
        fprintf(stderr,
                "Error submitting the audio packet to the decoder %d \n", ret);
      }
      ret = avcodec_receive_frame(dec_ctx_a_, decoded_frame_);
      // printf("dec_ctx_v_ %d %d frame->format %d\n",
      // decoded_frame_->width,
      //        decoded_frame_->height, decoded_frame_->pts);
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        return -1;
      else if (ret < 0) {
        fprintf(stderr, "Error during decoding\n");
      }
      data_size = av_get_bytes_per_sample(dec_ctx_a_->sample_fmt);
      if (data_size < 0) {
        /* This should not occur, checking just for paranoia */
        fprintf(stderr, "Failed to calculate data size\n");
      }
      uint8_t **converted_input_samples = NULL;

      init_converted_samples(&converted_input_samples, enc_ctx_a,
                             decoded_frame_->nb_samples);
      convert_samples((const uint8_t **)decoded_frame_->extended_data,
                      converted_input_samples, decoded_frame_->nb_samples,
                      resample_context);
      add_samples_to_fifo(fifo, converted_input_samples,
                          decoded_frame_->nb_samples);
      if (av_audio_fifo_size(fifo) < output_frame_size) {
        printf("returned\n");
        return 0;
      }
      if (avpacket == nullptr ||
          av_audio_fifo_size(fifo) >= output_frame_size) {
        break;
      }
    }
    while (av_audio_fifo_size(fifo) >= output_frame_size ||
           (avpacket == nullptr && av_audio_fifo_size(fifo) > 0)) {
      AVFrame *output_frame;
      const int frame_size =
          FFMIN(av_audio_fifo_size(fifo), enc_ctx_a->frame_size);

      int data_written;
      if (init_output_frame(&output_frame, enc_ctx_a, frame_size))
        return AVERROR_EXIT;
      if (av_audio_fifo_read(fifo, (void **)output_frame->data, frame_size) <
          frame_size) {

        fprintf(stderr, "Could not read data from FIFO\n");
        av_frame_free(&output_frame);
        return AVERROR_EXIT;
      }

      if (encode_audio_frame(output_frame, ofmt_ctx_, enc_ctx_a,
                             &data_written)) {
        av_frame_free(&output_frame);
        return AVERROR_EXIT;
      }
      av_frame_free(&output_frame);
      // Encode(1, decoded_frame_);
    }
  }
  printf("returned222222222\n");
  return 0;
}

int Transcode::Encode(int type, AVFrame *decoded_frame) {
  if (decoded_frame->data == NULL) {
    printf("data area is not alloced~\n");
  }
  int ret = av_frame_make_writable(decoded_frame);
  if (ret < 0)
    exit(1);
  if (type == 0) {
    ret = avcodec_send_frame(enc_ctx_v, decoded_frame);
    if (ret < 0) {
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        printf("d111111111111111~\n");
    }

    ret = avcodec_receive_packet(enc_ctx_v, encoded_pkt_);

    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
      return -1;
    else if (ret < 0) {
      fprintf(stderr, "Error during encoding\n");
      exit(1);
    }

    av_packet_rescale_ts(encoded_pkt_, enc_ctx_v->time_base,
                         ofmt_ctx_->streams[0]->time_base);

  } else if (type == 1) {
    ret = avcodec_send_frame(enc_ctx_a, decoded_frame);
    printf("ssssssssssssssize %d\n", enc_ctx_a->frame_size);
    if (ret < 0) {
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        printf("d111111111111111~\n");
    }
    while (ret >= 0) {
      ret = avcodec_receive_packet(enc_ctx_a, encoded_pkt_);
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        return -1;
      else if (ret < 0) {
        fprintf(stderr, "Error during encoding\n");
        exit(1);
      }
    }
    av_packet_rescale_ts(encoded_pkt_, enc_ctx_a->time_base,
                         ofmt_ctx_->streams[1]->time_base);
  }

  // if (t1_u_) {
  //   t1_p_ = encoded_pkt_->pts;
  //   t1_u_ = false;
  // }
  // encoded_pkt_->pts = encoded_pkt_->dts = (encoded_pkt_->pts - t1_p_) *
  // 90;
  // encoded_pkt_->stream_index = 0;
  // static int64_t first_dts_v, first_dts_a;
  // static int64_t dts_v, dts_a, dts_v_b, dts_a_b;
  // dts_v = av_gettime() / 1000;
  // if (first_dts_v == 0)
  //   first_dts_v = dts_v;
  // encoded_pkt_->pts = (dts_v - first_dts_v) * 90;
  // // printf(" encoded_pkt_->pts %lld\n", encoded_pkt_->pts);

  // encoded_pkt_->dts = encoded_pkt_->pts;
  // // if (dts_v_b >= encoded_pkt_->dts)
  // //   continue;
  // dts_v_b = encoded_pkt_->dts;

  // for (int i = 0; i < 8; ++i) {
  //   printf("%02x ", encoded_pkt_->data[i]);
  // }
  // printf("\n");
  encoded_pkt_->stream_index = 0;
  ret = av_interleaved_write_frame(ofmt_ctx_, encoded_pkt_);

  // av_packet_unref(&packet);
  av_packet_unref(encoded_pkt_);
}

static int select_sample_rate(const AVCodec *codec) {
  const int *p;
  int best_samplerate = 0;

  if (!codec->supported_samplerates)
    return 44100;

  p = codec->supported_samplerates;
  while (*p) {
    if (!best_samplerate || abs(44100 - *p) < abs(44100 - best_samplerate))
      best_samplerate = *p;
    p++;
  }
  return best_samplerate;
}
static int select_channel_layout(const AVCodec *codec) {
  const uint64_t *p;
  uint64_t best_ch_layout = 0;
  int best_nb_channels = 0;

  if (!codec->channel_layouts)
    return AV_CH_LAYOUT_STEREO;

  p = codec->channel_layouts;
  while (*p) {
    int nb_channels = av_get_channel_layout_nb_channels(*p);

    if (nb_channels > best_nb_channels) {
      best_ch_layout = *p;
      best_nb_channels = nb_channels;
    }
    p++;
  }
  return best_ch_layout;
}
static int check_sample_fmt(const AVCodec *codec,
                            enum AVSampleFormat sample_fmt) {
  const enum AVSampleFormat *p = codec->sample_fmts;

  while (*p != AV_SAMPLE_FMT_NONE) {
    if (*p == sample_fmt)
      return 1;
    p++;
  }
  return 0;
}
int Transcode::OpenOutput(char *filename) {
  AVStream *out_stream_v = nullptr;
  AVStream *out_stream_a = nullptr;
  AVCodec *encoder_v = nullptr;
  AVCodec *encoder_a = nullptr;
  avformat_alloc_output_context2(&ofmt_ctx_, NULL, NULL, filename);
  if (!ofmt_ctx_) {
    printf("Could not create output context\n");
    return -1;
  }
  out_stream_v = avformat_new_stream(ofmt_ctx_, NULL);
  out_stream_a = avformat_new_stream(ofmt_ctx_, NULL);

  if (!out_stream_v) {
    printf("Failed allocating output stream\n");
    return -1;
  }
  encoder_v = avcodec_find_encoder(AV_CODEC_ID_MSMPEG4V3);
  encoder_a = avcodec_find_encoder(AV_CODEC_ID_WMAV2);
  if (encoder_v == nullptr)
    printf("failed find encoder_v %d\n", AV_CODEC_ID_MSMPEG4V3);
  enc_ctx_v = avcodec_alloc_context3(encoder_v);
  enc_ctx_a = avcodec_alloc_context3(encoder_a);
  if (!enc_ctx_v) {
    printf("Failed to allocate the encoder_v context\n");
    return AVERROR(ENOMEM);
  }
  enc_ctx_a->sample_fmt = AV_SAMPLE_FMT_FLTP;
  if (!check_sample_fmt(encoder_a, enc_ctx_a->sample_fmt)) {
    fprintf(stderr, "Encoder does not support sample format %s",
            av_get_sample_fmt_name(enc_ctx_a->sample_fmt));
    exit(1);
  }
  enc_ctx_a->sample_rate = select_sample_rate(encoder_a);
  enc_ctx_a->channel_layout = select_channel_layout(encoder_a);
  enc_ctx_a->channels =
      av_get_channel_layout_nb_channels(enc_ctx_a->channel_layout);
  if (avcodec_open2(enc_ctx_a, encoder_a, NULL) < 0) {
    fprintf(stderr, "Could not open codec\n");
    exit(1);
  }
  enc_ctx_v->width = 1920;
  enc_ctx_v->height = 1080;
  if (encoder_v->pix_fmts)
    enc_ctx_v->pix_fmt = encoder_v->pix_fmts[0];
  AVRational rational;
  rational.num = 1;
  rational.den = 25;
  enc_ctx_v->time_base = rational;
  out_stream_v->time_base = enc_ctx_v->time_base;
  if (ofmt_ctx_->oformat->flags & AVFMT_GLOBALHEADER)
    enc_ctx_v->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  int ret = avcodec_open2(enc_ctx_v, encoder_v, NULL);
  if (ret < 0) {
    printf("Cannot open video encoder_v for stream #%u\n");
    return ret;
  }
  ret = avcodec_parameters_from_context(out_stream_v->codecpar, enc_ctx_v);
  if (ret < 0) {
    printf("Failed to copy codec context to out_stream_v codecpar context\n");
  }
  ret = avcodec_parameters_from_context(out_stream_a->codecpar, enc_ctx_a);
  if (ret < 0) {
    printf("Failed to copy codec context to out_stream_v codecpar context\n");
  }

  av_dump_format(ofmt_ctx_, 0, filename, 1);

  if (!(ofmt_ctx_->oformat->flags & AVFMT_NOFILE)) {
    ret = avio_open(&ofmt_ctx_->pb, filename, AVIO_FLAG_WRITE);
    if (ret < 0) {
      av_log(NULL, AV_LOG_ERROR, "Could not open output file '%s'", filename);
      return ret;
    }
  }
  ret = avformat_write_header(ofmt_ctx_, NULL);
  if (ret < 0) {
    av_log(NULL, AV_LOG_ERROR, "Error occurred when opening output file\n");
    return ret;
  }
  return 0;
}

int Transcode::CloseOutPut() {
  if (ofmt_ctx_ != nullptr) {
    av_write_trailer(ofmt_ctx_);
  }
  if ((enc_ctx_v) != NULL)
    avcodec_free_context(&enc_ctx_v);
  if (ofmt_ctx_ && !(ofmt_ctx_->oformat->flags & AVFMT_NOFILE))
    avio_closep(&ofmt_ctx_->pb);
  avformat_free_context(ofmt_ctx_);
  return 0;
}