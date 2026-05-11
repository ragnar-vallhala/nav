/** @type {import('next').NextConfig} */
const nextConfig = {
    reactStrictMode: true,
    // Enable standalone output for ultra-lightweight docker container bundles later if needed
    output: 'standalone',
};

export default nextConfig;
