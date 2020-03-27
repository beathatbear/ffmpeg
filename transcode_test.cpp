#include "transcode.h"
#include <memory>
#define H264_PATH "/home/wy/Downloads/h264.2/vif_bsd_1920x1080_%d.h264"
#define Haac_PATH "/home/wy/Downloads/aac/aac_%d.aac"

#include <chrono>
#include <iostream>
#include <sys/time.h>
#include <time.h>
inline int64_t GetTickCount() { // ms
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (((int64_t)ts.tv_sec) * 1000 + ((int64_t)ts.tv_nsec) / 1000000);
}

int main(int argc, char **argv) {
  printf("aaaaaaaaaaaaa\n");

  char *h264_file_data[300];
  int h264_file_data_length[300];
  char *aac_file_data[1270];
  int aac_file_data_length[1270];
  Transcode mTrancode;

  mTrancode.Decodec_init(1);

  mTrancode.OpenOutput("tt.wmv");
  mTrancode.init_resampler();
  mTrancode.init_fifo();

  for (int i = 0; i < 300; ++i) {
    char h264_name[100] = {'\0'};
    sprintf(h264_name, H264_PATH, (i % 300) + 1);
    FILE *h264_file = fopen(h264_name, "rb");
    if (h264_file == NULL) {
      printf("%s \n", h264_name);
      ++i;
      continue;
    }

    fseek(h264_file, 0, SEEK_END); //定位文件指针到文件尾。
    int h264_file_size = ftell(h264_file);
    fseek(h264_file, 0, SEEK_SET); //定位文件指针到文件头。
    h264_file_data_length[i] = h264_file_size;
    h264_file_data[i] = new char[h264_file_size];
    int h264_file_length =
        fread(h264_file_data[i], 1, h264_file_size, h264_file);

    if (h264_file_length != h264_file_size) {
      printf("h264_file_length != h264_file_size，%d %d\n", h264_file_length,
             h264_file_size);
    }
    fclose(h264_file);
  }

  for (int i = 0; i < 1270; ++i) {
    char aac_name[100] = {'\0'};
    sprintf(aac_name, Haac_PATH, (i % 1270));
    FILE *aac_file = fopen(aac_name, "rb");
    if (aac_file == NULL) {
      printf("%s \n", aac_name);
      ++i;
      continue;
    }

    fseek(aac_file, 0, SEEK_END); //定位文件指针到文件尾。
    int aac_file_size = ftell(aac_file);
    fseek(aac_file, 0, SEEK_SET); //定位文件指针到文件头。
    aac_file_data_length[i] = aac_file_size;
    aac_file_data[i] = new char[aac_file_size];
    int aac_file_length = fread(aac_file_data[i], 1, aac_file_size, aac_file);
    if (aac_file_length != aac_file_size) {
      printf("aac_file_length != aac_file_size，%d %d\n", aac_file_length,
             aac_file_size);
    }
    fclose(aac_file);
  }

  int count = 0;
  auto start = std::chrono::steady_clock::now();
  while (count < 1270) {
    /* std::shared_ptr<AVPacket> packet(
        static_cast<AVPacket *>(av_malloc(sizeof(AVPacket))), [&](AVPacket *p) {
          av_packet_unref(p);
          av_freep(&p);
        });
    av_new_packet(packet.get(), h264_file_data_length[count % 300]);
    uint8_t *data = (uint8_t *)h264_file_data[count % 300];
    int data_size = h264_file_data_length[count % 300];
    memcpy(packet->data, data, data_size);
    packet->pts = packet->dts = count;
    AVPacket video_packet;
    packet->stream_index = 0;
    av_packet_ref(&video_packet, packet.get());
    mTrancode.Decode(0, &video_packet);
    av_packet_unref(&video_packet); */

    std::shared_ptr<AVPacket> Apacket(
        static_cast<AVPacket *>(av_malloc(sizeof(AVPacket))), [&](AVPacket *p) {
          av_packet_unref(p);
          av_freep(&p);
        });
    av_new_packet(Apacket.get(), aac_file_data_length[count % 1270]);
    uint8_t *Adata = (uint8_t *)aac_file_data[count % 1270];
    int Adata_size = aac_file_data_length[count % 1270];
    memcpy(Apacket->data, Adata, Adata_size);
    Apacket->pts = Apacket->dts = count;
    AVPacket audio_packet;
    Apacket->stream_index = 1;
    av_packet_ref(&audio_packet, Apacket.get());

    mTrancode.Decode(1, &audio_packet);
    av_packet_unref(&audio_packet);

    count++;
    // printf("count  %d %d\n", count, data_size);
  }
  for (int i = 0; i < 300; ++i) {
    delete[] h264_file_data[i];
  }
  mTrancode.Decode(1, NULL);
  mTrancode.CloseOutPut();
  auto end = std::chrono::steady_clock::now();
  std::chrono::duration<double, std::micro> elapsed = end - start;
  std::cout << "time: " << elapsed.count() / 1000 << "ms" << std::endl;
}