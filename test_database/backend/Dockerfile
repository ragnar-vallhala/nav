FROM --platform=$BUILDPLATFORM node:20-alpine AS deps

WORKDIR /app

COPY backend/package.json backend/package-lock.json* ./
RUN npm ci --omit=dev

FROM node:20-alpine

WORKDIR /app

COPY --from=deps /app/package.json ./package.json
COPY --from=deps /app/package-lock.json* ./
COPY --from=deps /app/node_modules ./node_modules
COPY backend/src ./src
COPY nav/nav.mjs ./cli/nav.mjs
COPY vendor/NavHAL ./vendor/NavHAL

EXPOSE 14000
CMD ["npm", "start"]
