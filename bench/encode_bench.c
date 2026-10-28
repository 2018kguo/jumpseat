// Encoder benchmark: replays frames recorded with `jumpseat --record` through
// OpenH264 with the streamer's settings and reports encode time per frame,
// bitrate, and luma PSNR of the decoded stream against the input, so builds
// of OpenH264 (stock or patched) are judged on identical input.
//
//   encode_bench FRAMES.bgrx [key=value ...]
//   keys: usage (0 camera, 1 screen), complexity, loopfilter, qp, refs,
//         scd, bg, aq, ltr, threads, fps, repeat
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <libyuv/convert.h>
#include <wels/codec_api.h>

enum { W = 1280, H = 800 };

static double now_ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static int opt(int argc, char **argv, const char *key, int fallback) {
  size_t n = strlen(key);
  for (int i = 2; i < argc; i++)
    if (!strncmp(argv[i], key, n) && argv[i][n] == '=') return atoi(argv[i] + n + 1);
  return fallback;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s FRAMES.bgrx [key=value ...]\n", argv[0]);
    return 2;
  }
  FILE *in = fopen(argv[1], "rb");
  if (!in) { perror(argv[1]); return 1; }
  fseek(in, 0, SEEK_END);
  long frames = ftell(in) / (W * H * 4);
  fseek(in, 0, SEEK_SET);
  uint8_t *bgrx = malloc((size_t)frames * W * H * 4);
  if (fread(bgrx, (size_t)W * H * 4, frames, in) != (size_t)frames) { fprintf(stderr, "short read\n"); return 1; }
  fclose(in);
  // Convert once, up front: the benchmark times the encoder alone.
  size_t isize = W * H * 3 / 2;
  uint8_t *i420 = malloc((size_t)frames * isize);
  for (long f = 0; f < frames; f++) {
    uint8_t *y = i420 + f * isize, *u = y + W * H, *v = u + W * H / 4;
    ARGBToI420(bgrx + f * (size_t)W * H * 4, W * 4, y, W, u, W / 2, v, W / 2, W, H);
  }
  free(bgrx);

  int qp = opt(argc, argv, "qp", 17), fps = opt(argc, argv, "fps", 60), repeat = opt(argc, argv, "repeat", 1);
  ISVCEncoder *enc;
  WelsCreateSVCEncoder(&enc);
  SEncParamExt p;
  (*enc)->GetDefaultParams(enc, &p);
  p.iUsageType = opt(argc, argv, "usage", 0) ? SCREEN_CONTENT_REAL_TIME : CAMERA_VIDEO_REAL_TIME;
  p.iPicWidth = W; p.iPicHeight = H; p.fMaxFrameRate = fps;
  p.iRCMode = RC_QUALITY_MODE; p.iTargetBitrate = 50000000; p.iMaxBitrate = UNSPECIFIED_BIT_RATE;
  p.iMinQp = qp; p.iMaxQp = 51;
  p.bEnableFrameSkip = 0; p.uiIntraPeriod = 0;
  p.iMultipleThreadIdc = opt(argc, argv, "threads", 1);
  p.iComplexityMode = (ECOMPLEXITY_MODE)opt(argc, argv, "complexity", 0);
  p.iLoopFilterDisableIdc = opt(argc, argv, "loopfilter", 1);
  int v;
  if ((v = opt(argc, argv, "refs", -1)) >= 0) p.iNumRefFrame = v;
  if ((v = opt(argc, argv, "scd", -1)) >= 0) p.bEnableSceneChangeDetect = v;
  if ((v = opt(argc, argv, "bg", -1)) >= 0) p.bEnableBackgroundDetection = v;
  if ((v = opt(argc, argv, "aq", -1)) >= 0) p.bEnableAdaptiveQuant = v;
  if ((v = opt(argc, argv, "ltr", -1)) >= 0) p.bEnableLongTermReference = v;
  p.eSpsPpsIdStrategy = CONSTANT_ID;
  p.iSpatialLayerNum = 1; p.iTemporalLayerNum = 1;
  p.sSpatialLayers[0].iVideoWidth = W; p.sSpatialLayers[0].iVideoHeight = H;
  p.sSpatialLayers[0].fFrameRate = fps;
  p.sSpatialLayers[0].iSpatialBitrate = p.iTargetBitrate;
  p.sSpatialLayers[0].iMaxSpatialBitrate = UNSPECIFIED_BIT_RATE;
  p.sSpatialLayers[0].iDLayerQp = qp;
  p.sSpatialLayers[0].sSliceArgument.uiSliceMode = SM_SINGLE_SLICE;
  if ((*enc)->InitializeExt(enc, &p) != 0) { fprintf(stderr, "encoder init failed\n"); return 1; }
  int format = videoFormatI420;
  (*enc)->SetOption(enc, ENCODER_OPTION_DATAFORMAT, &format);

  ISVCDecoder *dec;
  WelsCreateDecoder(&dec);
  SDecodingParam dp;
  memset(&dp, 0, sizeof dp);
  dp.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_DEFAULT;
  (*dec)->Initialize(dec, &dp);

  double encode_ms = 0, worst = 0, squared = 0;
  long bytes = 0, decoded = 0, encoded = 0;
  uint8_t *bitstream = malloc(W * H * 2);
  for (int r = 0; r < repeat; r++) {
    for (long f = 0; f < frames; f++) {
      uint8_t *y = i420 + f * isize;
      SSourcePicture pic;
      memset(&pic, 0, sizeof pic);
      pic.iColorFormat = videoFormatI420; pic.iPicWidth = W; pic.iPicHeight = H;
      pic.iStride[0] = W; pic.iStride[1] = W / 2; pic.iStride[2] = W / 2;
      pic.pData[0] = y; pic.pData[1] = y + W * H; pic.pData[2] = y + W * H * 5 / 4;
      pic.uiTimeStamp = (long long)(r * frames + f) * 1000 / fps;
      SFrameBSInfo info;
      memset(&info, 0, sizeof info);
      double t0 = now_ms();
      (*enc)->EncodeFrame(enc, &pic, &info);
      double took = now_ms() - t0;
      encode_ms += took;
      if (took > worst) worst = took;
      encoded++;
      if (info.eFrameType == videoFrameTypeSkip) continue;
      int size = 0;
      for (int l = 0; l < info.iLayerNum; l++) {
        SLayerBSInfo *layer = &info.sLayerInfo[l];
        int n = 0;
        for (int k = 0; k < layer->iNalCount; k++) n += layer->pNalLengthInByte[k];
        memcpy(bitstream + size, layer->pBsBuf, n);
        size += n;
      }
      bytes += size;
      if (r > 0) continue;
      // Decode and compare luma with the input.
      uint8_t *planes[3] = {0};
      SBufferInfo out;
      memset(&out, 0, sizeof out);
      (*dec)->DecodeFrameNoDelay(dec, bitstream, size, planes, &out);
      if (out.iBufferStatus == 1) {
        int stride = out.UsrData.sSystemBuffer.iStride[0];
        for (int row = 0; row < H; row++)
          for (int col = 0; col < W; col++) {
            double d = (double)planes[0][row * stride + col] - y[row * W + col];
            squared += d * d;
          }
        decoded++;
      }
    }
  }
  double mse = squared / ((double)decoded * W * H);
  printf("frames %ld  encode %.3f ms/frame (worst %.1f)  %.0f kbit/s at %d fps  psnr %.2f dB\n",
         encoded, encode_ms / encoded, worst, bytes * 8.0 / (encoded / (double)fps) / 1000, fps,
         10 * log10(255.0 * 255.0 / (mse > 1e-9 ? mse : 1e-9)));
  return 0;
}
