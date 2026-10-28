// FFmpeg's H.264 decoder for encode_bench's PSNR (the one Chrome uses).
#include <stdint.h>
#include <libavcodec/avcodec.h>

static AVCodecContext *ctx;
static AVPacket *pkt;
static AVFrame *frame;

int decode_side_open(void) {
  const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
  ctx = avcodec_alloc_context3(codec);
  ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
  pkt = av_packet_alloc();
  frame = av_frame_alloc();
  return avcodec_open2(ctx, codec, 0);
}

// Decodes one access unit; on a picture, points *luma at its Y plane.
int decode_side_frame(uint8_t *data, int size, uint8_t **luma, int *stride) {
  pkt->data = data; pkt->size = size;
  if (avcodec_send_packet(ctx, pkt) < 0) return 0;
  if (avcodec_receive_frame(ctx, frame) < 0) return 0;
  *luma = frame->data[0]; *stride = frame->linesize[0];
  return 1;
}
