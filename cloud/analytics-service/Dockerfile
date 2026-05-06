FROM golang:1.22-alpine AS builder
WORKDIR /app

COPY go.mod ./
RUN /usr/local/go/bin/go mod download

COPY . .
RUN CGO_ENABLED=0 GOOS=linux GOARCH=amd64 /usr/local/go/bin/go build -o /analytics-service ./

FROM alpine:3.20
RUN adduser -D -u 10001 appuser
USER appuser
WORKDIR /app

COPY --from=builder /analytics-service /analytics-service
EXPOSE 3000
ENTRYPOINT ["/analytics-service"]
