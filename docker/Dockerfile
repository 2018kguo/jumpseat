# jumpseat demo: a 1280x800 desktop with Chromium, streamed by jumpseat.
#   docker build -f docker/Dockerfile -t jumpseat .
#   docker run --rm -p 8080:8080 -e JUMPSEAT_TOKEN=secret jumpseat
#   open http://localhost:8080/?token=secret
FROM oven/bun:1 AS client
WORKDIR /src
COPY client client
RUN cd client && bun build jumpseat.ts --format=esm --target=browser --outfile /out/jumpseat.js

# OpenH264 with openh264/screen-fme.patch: screen mode's scroll search at
# camera mode's CPU (BENCHMARKS.md).
FROM debian:bookworm AS openh264
RUN apt-get update && apt-get install -y --no-install-recommends build-essential nasm git ca-certificates
COPY openh264 /patches
RUN git clone --depth 1 --branch v2.6.0 https://github.com/cisco/openh264 /openh264 && cd /openh264 \
    && git apply /patches/screen-fme.patch \
    && make -j"$(nproc)" BUILDTYPE=Release PREFIX=/usr/local install-static >/dev/null

FROM golang:1.26-bookworm AS server
RUN apt-get update && apt-get install -y --no-install-recommends libyuv-dev libx264-dev
COPY --from=openh264 /usr/local/include/wels /usr/local/include/wels
COPY --from=openh264 /usr/local/lib/libopenh264.a /usr/local/lib/
WORKDIR /src
COPY server server
COPY --from=client /out/jumpseat.js server/web/jumpseat.js
RUN cd server && go test ./... && go build -ldflags='-s -w' -o /jumpseat .

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
      xvfb openbox chromium xdotool xclip procps libyuv0 fonts-dejavu-core ca-certificates \
    && rm -rf /var/lib/apt/lists/*
COPY --from=server /jumpseat /usr/local/bin/jumpseat
ENV JUMPSEAT_OH264=usage=1
COPY docker/start.sh /usr/local/bin/start.sh
EXPOSE 8080
CMD ["/usr/local/bin/start.sh"]
