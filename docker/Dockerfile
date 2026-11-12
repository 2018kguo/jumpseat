# jumpseat demo: a desktop with Chromium, streamed by jumpseat. 1280x800, or up to
# 2560x1600 following the viewer with -e JUMPSEAT_RESIZE=1.
#   docker build -f docker/Dockerfile -t jumpseat .
#   docker run --rm -p 8080:8080 -e JUMPSEAT_TOKEN=secret jumpseat
#   open http://localhost:8080/?token=secret
FROM oven/bun:1 AS client
WORKDIR /src
COPY client client
RUN cd client && bun build jumpseat.ts --format=esm --target=browser --outfile /out/jumpseat.js

FROM golang:1.26-bookworm AS server
RUN apt-get update && apt-get install -y --no-install-recommends nasm libyuv-dev libopus-dev
# h264/: jumpseat's encoder (OpenH264's, trimmed and tuned for desktops).
COPY h264 /src/h264
RUN make -C /src/h264 -j"$(nproc)" -s
WORKDIR /src
COPY server server
COPY --from=client /out/jumpseat.js server/web/jumpseat.js
# bench/ builds with GOFLAGS=-tags=pprof for the Go profiler.
ARG GOFLAGS=""
RUN cd server && go test ./... && go build -ldflags='-s -w' -o /jumpseat .

# The bare binary, for releases: docker build --target bin -o out .
FROM scratch AS bin
COPY --from=server /jumpseat /jumpseat

# The wallpaper, rendered here so the runtime image needs no SVG renderer.
FROM debian:bookworm-slim AS desktop
RUN apt-get update && apt-get install -y --no-install-recommends librsvg2-bin fonts-dejavu-core
COPY docker/desktop/wallpaper.svg /wallpaper.svg
RUN rsvg-convert /wallpaper.svg -o /wallpaper.png
# Debian's Xvfb with scroll axes on its XTest pointer (patch from voxl, MIT),
# so the streamer can inject pixel-precise scrolling; stock Xvfb gets wheel
# clicks instead. Only Xvfb is built; the xvfb package below brings its
# libraries and keymaps.
FROM debian:bookworm AS xvfb
RUN sed -i 's/^Types: deb$/Types: deb deb-src/' /etc/apt/sources.list.d/debian.sources \
    && apt-get update && apt-get build-dep -y xorg-server && apt-get install -y dpkg-dev meson ninja-build
WORKDIR /src
RUN apt-get source xorg-server && mv xorg-server-*/ xorg-server
COPY docker/patches/xorg-server-xtest-scroll.patch /src/
RUN cd xorg-server && patch -p1 < ../xorg-server-xtest-scroll.patch \
    && meson setup build --prefix=/usr -Dxorg=false -Dxephyr=false -Dxnest=false \
         -Dxvfb=true -Dglamor=false -Dudev=false -Dsystemd_logind=false -Ddocs=false -Ddevel-docs=false \
         -Dxkb_dir=/usr/share/X11/xkb -Dxkb_output_dir=/var/lib/xkb -Dxkb_bin_dir=/usr/bin \
    && ninja -C build hw/vfb/Xvfb && cp build/hw/vfb/Xvfb /Xvfb

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
      xvfb openbox chromium xdotool xclip procps libyuv0 fonts-dejavu-core ca-certificates \
      hsetroot x11-utils pulseaudio pulseaudio-utils \
    && rm -rf /var/lib/apt/lists/*
COPY --from=desktop /wallpaper.png /usr/share/jumpseat/wallpaper.png
COPY docker/desktop/themerc /usr/share/themes/jumpseat/openbox-3/themerc
RUN sed -i 's|<name>Clearlooks</name>|<name>jumpseat</name>|' /etc/xdg/openbox/rc.xml \
    && grep -q '<name>jumpseat</name>' /etc/xdg/openbox/rc.xml
COPY --from=xvfb /Xvfb /usr/bin/Xvfb
COPY --from=server /jumpseat /usr/local/bin/jumpseat
COPY docker/start.sh /usr/local/bin/start.sh
EXPOSE 8080
CMD ["/usr/local/bin/start.sh"]
