import type { Metadata } from "next";
import { Geist, Geist_Mono, Noto_Serif_SC } from "next/font/google";
import "./globals.css";

const sans = Geist({ variable: "--font-sans", subsets: ["latin"] });
const mono = Geist_Mono({ variable: "--font-mono", subsets: ["latin"] });
const serif = Noto_Serif_SC({ variable: "--font-serif", subsets: ["latin"], weight: ["500", "600", "700", "900"] });

export const metadata: Metadata = {
  title: "万象图鉴 · AscendSpire 内容工作台",
  description: "登仙路的本地数据图鉴、卡牌编辑器与 LLM JSON 内容入口。",
};

export default function RootLayout({ children }: Readonly<{ children: React.ReactNode }>) {
  return <html lang="zh-CN"><body className={`${sans.variable} ${mono.variable} ${serif.variable}`}>{children}</body></html>;
}
