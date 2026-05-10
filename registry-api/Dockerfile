# Stage 1: Build stage
FROM golang:1.21-alpine AS builder

WORKDIR /app

# Install build toolchain utilities
RUN apk add --no-cache git ca-certificates

# Download Module Dependencies
COPY go.mod ./
# Clone Application Manifest Source
COPY . .

# Dynamically construct healthy checksums prior to build
RUN go mod tidy

# Compile hardened static binary
RUN CGO_ENABLED=0 GOOS=linux go build -ldflags="-s -w" -o /registry-server cmd/server/main.go

# Stage 2: Final lightweight runner
FROM alpine:3.19

WORKDIR /root/

# Inherit only the standalone runtime binary and critical TLS authorities
COPY --from=builder /etc/ssl/certs/ca-certificates.crt /etc/ssl/certs/
COPY --from=builder /registry-server .

# Standard API binding boundary
EXPOSE 8081

# Command orbit entrypoint
CMD ["./registry-server"]
