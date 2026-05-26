FROM node:20-alpine

WORKDIR /app

COPY backend/package.json backend/package-lock.json* ./
RUN npm ci --omit=dev

COPY backend/src ./src
COPY nav/nav.mjs ./cli/nav.mjs
COPY vendor/NavHAL ./vendor/NavHAL

EXPOSE 14000
CMD ["npm", "start"]
