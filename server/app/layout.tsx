export const metadata = { title: "AI Passport Farm" };

export default function RootLayout({ children }: { children: React.ReactNode }) {
  return (
    <html lang="zh-CN">
      <body style={{ fontFamily: "ui-sans-serif, system-ui", margin: 24 }}>
        {children}
      </body>
    </html>
  );
}
