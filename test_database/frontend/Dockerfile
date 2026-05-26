FROM node:20-alpine

WORKDIR /app

COPY package.json package-lock.json* ./
RUN npm ci

COPY index.html ./index.html
COPY vite.config.js ./vite.config.js
COPY public ./public
COPY src ./src

EXPOSE 13000
CMD ["npm", "start"]
