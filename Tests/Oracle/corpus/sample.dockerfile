# A representative multi-stage build: FROM/AS, RUN, COPY, ENV, ARG, CMD.
ARG VERSION=1.0

FROM golang:1.23 AS builder
WORKDIR /src
COPY go.mod go.sum ./
RUN go mod download
COPY . .
RUN go build -o /out/sample ./cmd/sample

FROM alpine:3.20
LABEL maintainer="sample@example.com"
ENV APP_HOME=/app
EXPOSE 8080
COPY --from=builder /out/sample ${APP_HOME}/sample
USER nobody
ENTRYPOINT ["/app/sample"]
CMD ["--port", "8080"]
