import type { Metadata } from "next";
import "./globals.css";

export const metadata: Metadata = {
  title: "Nav Registry | Industrial Package Index",
  description: "The high-performance, terminal-optimized robotics indexing suite.",
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="en">
      <body>{children}</body>
    </html>
  );
}
