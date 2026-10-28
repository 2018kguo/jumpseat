# jumpseat demo: a 1280x800 desktop with Chromium, streamed by jumpseat.
#   docker build -f docker/Dockerfile -t jumpseat .
#   docker run --rm -p 8080:8080 -e JUMPSEAT_TOKEN=secret jumpseat
#   open http://localhost:8080/?token=secret
FROM oven/bun:1 AS client
WORKDIR /src
COPY client client
RUN cd client && bun build jumpseat.ts --format=esm --target=browser --outfile /out/jumpseat.js

FROM golang:1.26-bookworm AS server
RUN apt-get update && apt-get install -y --no-install-recommends libopenh264-dev libyuv-dev libx264-dev
WORKDIR /src
COPY server server
COPY --from=client /out/jumpseat.js server/web/jumpseat.js
RUN cd server && go test ./... && go build -ldflags='-s -w' -o /jumpseat .

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
      xvfb openbox chromium xdotool xclip procps libopenh264-7 libyuv0 fonts-dejavu-core ca-certificates \
    && rm -rf /var/lib/apt/lists/*
COPY --from=server /jumpseat /usr/local/bin/jumpseat
COPY docker/start.sh /usr/local/bin/start.sh
EXPOSE 8080
CMD ["/usr/local/bin/start.sh"]
