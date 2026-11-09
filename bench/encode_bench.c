// Encoder benchmark: replays frames recorded with `jumpseat --record` through
// jumpseat's H.264 encoder (h264/, or a stock OpenH264 build) with the
// streamer's settings and reports encode time per frame,
// bitrate, and luma PSNR of the decoded stream against the input, so builds
// of OpenH264 (stock or patched) are judged on identical input.
//
//   encode_bench FRAMES.bgrx [key=value ...]
//   keys: enc (0 OpenH264, 1 x264 as the streamer sets it up), crf and
//         xthreads (x264; 0 = auto, as the streamer runs it),
//         usage (0 camera, 1 screen), complexity, qp, refs,
//         scd, bg, aq, ltr, cabac (1, as the streamer; 0 CAVLC), threads, fps, repeat, rest (then encode the last
//         frame that many more times: the quality left once motion stops),
//         kbps (cap the bitrate as the streamer's flow control does, so the
//         rate control's capped paths run)
// "cpu" is process CPU time per frame: x264 runs sliced threads, so its
// wall time understates what it costs.
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

static double cpu_ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t);
  return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

// bench/x264_side.c: x264.h and OpenH264's headers define the same enums.
void *x264_side_open(int width, int height, int fps, int crf, int threads);
int x264_side_encode(void *x, uint8_t *i420, long long pts, uint8_t **out);
// bench/decode_side.c
int decode_side_open(void);
int decode_side_frame(uint8_t *data, int size, uint8_t **luma, int *stride);

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
  int rest = opt(argc, argv, "rest", 0);
  double last_psnr = 0;
  int use_x264 = opt(argc, argv, "enc", 0), perframe = opt(argc, argv, "perframe", 0);
  void *x = use_x264 ? x264_side_open(W, H, fps, opt(argc, argv, "crf", 25), opt(argc, argv, "xthreads", 0)) : 0;
  if (use_x264 && !x) { fprintf(stderr, "x264 open failed\n"); return 1; }
  ISVCEncoder *enc;
  WelsCreateSVCEncoder(&enc);
  SEncParamExt p;
  (*enc)->GetDefaultParams(enc, &p);
  p.iUsageType = opt(argc, argv, "usage", 0) ? SCREEN_CONTENT_REAL_TIME : CAMERA_VIDEO_REAL_TIME;
  p.iPicWidth = W; p.iPicHeight = H; p.fMaxFrameRate = fps;
  p.iRCMode = (RC_MODES)opt(argc, argv, "rc", RC_QUALITY_MODE); p.iTargetBitrate = 50000000; p.iMaxBitrate = UNSPECIFIED_BIT_RATE;
  p.iMinQp = qp; p.iMaxQp = 51;
  p.bEnableFrameSkip = 0; p.uiIntraPeriod = 0;
  p.iMultipleThreadIdc = opt(argc, argv, "threads", 1);
  p.iComplexityMode = (ECOMPLEXITY_MODE)opt(argc, argv, "complexity", 0);
  int v;
  if ((v = opt(argc, argv, "refs", -1)) >= 0) p.iNumRefFrame = v;
  if ((v = opt(argc, argv, "scd", -1)) >= 0) p.bEnableSceneChangeDetect = v;
  if ((v = opt(argc, argv, "bg", -1)) >= 0) p.bEnableBackgroundDetection = v;
  if ((v = opt(argc, argv, "aq", -1)) >= 0) p.bEnableAdaptiveQuant = v;
  if ((v = opt(argc, argv, "ltr", -1)) >= 0) p.bEnableLongTermReference = v;
  p.iEntropyCodingModeFlag = opt(argc, argv, "cabac", 1); // as the streamer; cabac=0 for CAVLC
  p.eSpsPpsIdStrategy = CONSTANT_ID;
  p.iSpatialLayerNum = 1; p.iTemporalLayerNum = 1;
  p.sSpatialLayers[0].iVideoWidth = W; p.sSpatialLayers[0].iVideoHeight = H;
  p.sSpatialLayers[0].fFrameRate = fps;
  p.sSpatialLayers[0].iSpatialBitrate = p.iTargetBitrate;
  p.sSpatialLayers[0].iMaxSpatialBitrate = UNSPECIFIED_BIT_RATE;
  p.sSpatialLayers[0].iDLayerQp = qp;
  p.sSpatialLayers[0].sSliceArgument.uiSliceMode = SM_SINGLE_SLICE;
  if (!use_x264 && (*enc)->InitializeExt(enc, &p) != 0) { fprintf(stderr, "encoder init failed\n"); return 1; }
  int format = videoFormatI420;
  (*enc)->SetOption(enc, ENCODER_OPTION_DATAFORMAT, &format);
  // kbps=N caps the rate as server/encoder_openh264.go set_max_kbps does;
  // capat=F applies it before frame F (the streamer caps only once its flow
  // control has measured the link, so its first IDR is never capped).
  int kbps = opt(argc, argv, "kbps", 0), capat = opt(argc, argv, "capat", 0);

  if (decode_side_open() < 0) { fprintf(stderr, "decoder open failed\n"); return 1; }

  double encode_ms = 0, encode_cpu = 0, worst = 0, squared = 0;
  long bytes = 0, decoded = 0, encoded = 0;
  uint8_t *bitstream = malloc(W * H * 2);
  for (int r = 0; r < repeat; r++) {
    for (long n = 0; n < frames + (r == repeat - 1 ? rest : 0); n++) {
      long f = n < frames ? n : frames - 1;
      uint8_t *y = i420 + f * isize;
      SSourcePicture pic;
      memset(&pic, 0, sizeof pic);
      pic.iColorFormat = videoFormatI420; pic.iPicWidth = W; pic.iPicHeight = H;
      pic.iStride[0] = W; pic.iStride[1] = W / 2; pic.iStride[2] = W / 2;
      pic.pData[0] = y; pic.pData[1] = y + W * H; pic.pData[2] = y + W * H * 5 / 4;
      pic.uiTimeStamp = (long long)(r * frames + n) * 1000 / fps;
      SFrameBSInfo info;
      memset(&info, 0, sizeof info);
      uint8_t *nal = 0;
      int size = 0;
      if (kbps > 0 && !use_x264 && r == 0 && n == capat) {
        SBitrateInfo rate;
        memset(&rate, 0, sizeof rate);
        rate.iLayer = SPATIAL_LAYER_ALL;
        rate.iBitrate = kbps * 1000;
        (*enc)->SetOption(enc, ENCODER_OPTION_BITRATE, &rate);
      }
      double t0 = now_ms(), c0 = cpu_ms();
      if (use_x264) size = x264_side_encode(x, y, pic.uiTimeStamp, &nal);
      else (*enc)->EncodeFrame(enc, &pic, &info);
      double took = now_ms() - t0;
      encode_cpu += cpu_ms() - c0;
      encode_ms += took;
      if (took > worst) worst = took;
      encoded++;
      if (use_x264) {
        if (size <= 0) continue;
        memcpy(bitstream, nal, size);
      } else {
        if (info.eFrameType == videoFrameTypeSkip) continue;
        for (int l = 0; l < info.iLayerNum; l++) {
          SLayerBSInfo *layer = &info.sLayerInfo[l];
          int n = 0;
          for (int k = 0; k < layer->iNalCount; k++) n += layer->pNalLengthInByte[k];
          memcpy(bitstream + size, layer->pBsBuf, n);
          size += n;
        }
      }
      bytes += size;
      if (r > 0) continue;
      // Decode and compare luma with the input.
      uint8_t *luma;
      int stride;
      if (decode_side_frame(bitstream, size, &luma, &stride)) {
        double frame = 0;
        for (int row = 0; row < H; row++)
          for (int col = 0; col < W; col++) {
            double d = (double)luma[row * stride + col] - y[row * W + col];
            frame += d * d;
          }
        squared += frame;
        last_psnr = 10 * log10(255.0 * 255.0 * W * H / (frame > 1e-9 ? frame : 1e-9));
        if (perframe) fprintf(stderr, "frame %ld %d bytes psnr %.2f\n", f, size,
                              10 * log10(255.0 * 255.0 * W * H / (frame > 1e-9 ? frame : 1e-9)));
        decoded++;
      }
    }
  }
  double mse = squared / ((double)decoded * W * H);
  if (rest) printf("at rest %.2f dB  ", last_psnr);
  printf("frames %ld  encode %.3f ms/frame (cpu %.3f, worst %.1f)  %.0f kbit/s at %d fps  psnr %.2f dB\n",
         encoded, encode_ms / encoded, encode_cpu / encoded, worst, bytes * 8.0 / (encoded / (double)fps) / 1000, fps,
         10 * log10(255.0 * 255.0 / (mse > 1e-9 ? mse : 1e-9)));
  return 0;
}
