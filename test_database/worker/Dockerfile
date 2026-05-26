FROM node:20-bookworm-slim

WORKDIR /app

RUN apt-get update \
  && apt-get install -y --no-install-recommends \
    binutils-arm-none-eabi \
    gcc-arm-none-eabi \
    ca-certificates \
  && rm -rf /var/lib/apt/lists/*

COPY worker/package.json ./package.json
RUN npm install --omit=dev

COPY worker/src ./src

CMD ["npm", "start"]
