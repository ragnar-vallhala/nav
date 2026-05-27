FROM --platform=$BUILDPLATFORM node:20-bookworm-slim AS deps

WORKDIR /app

COPY worker/package.json worker/package-lock.json* ./
RUN npm ci --omit=dev

FROM node:20-bookworm-slim

WORKDIR /app

RUN apt-get update \
  && apt-get install -y --no-install-recommends \
    binutils-arm-none-eabi \
    gcc-arm-none-eabi \
    ca-certificates \
  && rm -rf /var/lib/apt/lists/*

COPY --from=deps /app/package.json ./package.json
COPY --from=deps /app/package-lock.json* ./
COPY --from=deps /app/node_modules ./node_modules
COPY worker/src ./src

CMD ["npm", "start"]
