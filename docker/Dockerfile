# agentview demo: a 1280x800 desktop with Chromium, streamed by agentview.
#   docker build -f docker/Dockerfile -t agentview .
#   docker run --rm -p 8080:8080 -e AGENTVIEW_TOKEN=secret agentview
#   open http://localhost:8080/?token=secret
FROM oven/bun:1 AS client
WORKDIR /src
COPY client client
RUN cd client && bun build agentview.ts --format=esm --target=browser --outfile /out/agentview.js

FROM golang:1.26-bookworm AS server
RUN apt-get update && apt-get install -y --no-install-recommends libopenh264-dev libyuv-dev libx264-dev
WORKDIR /src
COPY server server
COPY --from=client /out/agentview.js server/web/agentview.js
RUN cd server && go test ./... && go build -ldflags='-s -w' -o /agentview .

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
      xvfb openbox chromium xdotool xclip procps libopenh264-7 libyuv0 fonts-dejavu-core ca-certificates \
    && rm -rf /var/lib/apt/lists/*
COPY --from=server /agentview /usr/local/bin/agentview
COPY docker/start.sh /usr/local/bin/start.sh
EXPOSE 8080
CMD ["/usr/local/bin/start.sh"]
