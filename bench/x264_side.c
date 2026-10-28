// The streamer's x264 (server/encoder.go) for encode_bench: ultrafast,
// zerolatency, CRF, constant-rate timestamps, baseline, BT.601 tagged.
#include <stdint.h>
#include <stdlib.h>
#include <x264.h>

typedef struct { x264_t *enc; int w, h; } side;

void *x264_side_open(int width, int height, int fps, int crf, int threads) {
  x264_param_t p;
  x264_param_default_preset(&p, "ultrafast", "zerolatency");
  p.i_log_level = X264_LOG_WARNING;
  p.i_width = width; p.i_height = height; p.i_csp = X264_CSP_I420;
  p.i_fps_num = fps; p.i_fps_den = 1;
  p.i_timebase_num = 1; p.i_timebase_den = 1000;
  p.rc.i_rc_method = X264_RC_CRF; p.rc.f_rf_constant = crf;
  p.i_keyint_max = X264_KEYINT_MAX_INFINITE; p.b_repeat_headers = 1; p.b_annexb = 1;
  p.rc.i_vbv_max_bitrate = 100000; p.rc.i_vbv_buffer_size = 100000;
  p.vui.i_colorprim = 6; p.vui.i_transfer = 6; p.vui.i_colmatrix = 6;
  if (threads > 0) p.i_threads = threads;
  // X264_NO_AVX512=1: what x264's AVX-512 kernels are worth on this CPU.
  if (getenv("X264_NO_AVX512")) p.cpu &= ~X264_CPU_AVX512;
  if (x264_param_apply_profile(&p, "baseline") < 0) return 0;
  side *s = malloc(sizeof *s);
  s->enc = x264_encoder_open(&p); s->w = width; s->h = height;
  return s->enc ? s : 0;
}

// Encodes one I420 frame; returns its size and points *out at the NALs.
int x264_side_encode(void *x, uint8_t *i420, long long pts, uint8_t **out) {
  side *s = x;
  x264_picture_t in, got;
  x264_picture_init(&in);
  in.img.i_csp = X264_CSP_I420; in.img.i_plane = 3;
  in.img.plane[0] = i420; in.img.plane[1] = i420 + s->w * s->h; in.img.plane[2] = i420 + s->w * s->h * 5 / 4;
  in.img.i_stride[0] = s->w; in.img.i_stride[1] = in.img.i_stride[2] = s->w / 2;
  in.i_pts = pts;
  x264_nal_t *nal; int n;
  int size = x264_encoder_encode(s->enc, &nal, &n, &in, &got);
  if (size > 0) *out = nal[0].p_payload;
  return size;
}
