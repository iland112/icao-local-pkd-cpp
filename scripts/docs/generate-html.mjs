#!/usr/bin/env node
/**
 * Markdown → HTML 변환 스크립트
 * docs/*.md → docs/html/*.html (프로젝트 브랜드 스타일 적용)
 */

import { readFileSync, writeFileSync, mkdirSync, readdirSync, copyFileSync, existsSync } from 'fs';
import { join, basename } from 'path';
import { marked } from 'marked';

const DOCS_DIR = join(import.meta.dirname, '../../docs');
const OUT_DIR = join(DOCS_DIR, 'html');

// marked 설정
marked.setOptions({
  gfm: true,
  breaks: false,
});

// 문서 제목 매핑
const TITLES = {
  'README': 'Documentation Index',
  'DEPLOYMENT_GUIDE': 'Deployment Guide',
  'ARCHITECTURE': 'System Architecture & Design Principles',
  'ICAO_COMPLIANCE_MANUAL': 'ICAO Compliance Manual',
  'FRONTEND_GUIDE': 'Frontend Guide',
  'API_CLIENT_GUIDE': 'API Client Guide',
  'CERTIFICATE_PROCESSING_GUIDE': 'Certificate Processing Guide',
  'API_DEVELOPER_MANUAL': 'API Developer Manual',
  'PA_API_GUIDE': 'PA API Guide',
  'DEVELOPMENT_GUIDE': 'Development Guide',
  'AI_ANALYSIS_DASHBOARD_GUIDE': 'AI Analysis Dashboard Guide',
  'COMPETITIVE_ANALYSIS': 'Competitive Analysis',
  'DOCKER_BUILD_CACHE': 'Docker Build Cache Troubleshooting',
  'EAC_SERVICE_IMPLEMENTATION_PLAN': 'EAC Service Implementation Plan',
  'EULA': 'End User License Agreement',
  'ICAO_PKD_COST_ANALYSIS': 'ICAO PKD Cost Analysis',
  'LDAP_QUERY_GUIDE': 'LDAP Query Guide',
  'LICENSE_COMPLIANCE': 'License Compliance',
  'VERSION_HISTORY': 'Version History',
};

// 사이드바용 문서 카테고리
const CATEGORIES = [
  {
    name: 'Getting Started',
    docs: ['README', 'DEVELOPMENT_GUIDE', 'DEPLOYMENT_GUIDE'],
  },
  {
    name: 'API & Integration',
    docs: ['API_DEVELOPER_MANUAL', 'PA_API_GUIDE', 'API_CLIENT_GUIDE'],
  },
  {
    name: 'Architecture & Design',
    docs: ['ARCHITECTURE', 'FRONTEND_GUIDE'],
  },
  {
    name: 'Compliance & Security',
    docs: ['ICAO_COMPLIANCE_MANUAL', 'CERTIFICATE_PROCESSING_GUIDE'],
  },
  {
    name: 'AI & Analysis',
    docs: ['AI_ANALYSIS_DASHBOARD_GUIDE', 'COMPETITIVE_ANALYSIS'],
  },
  {
    name: 'Reference',
    docs: ['ICAO_PKD_COST_ANALYSIS', 'EAC_SERVICE_IMPLEMENTATION_PLAN', 'DOCKER_BUILD_CACHE', 'LDAP_QUERY_GUIDE', 'VERSION_HISTORY'],
  },
  {
    name: 'Legal',
    docs: ['EULA', 'LICENSE_COMPLIANCE'],
  },
];

function buildSidebar(currentDoc) {
  let html = '';
  for (const cat of CATEGORIES) {
    html += `<div class="nav-category">${cat.name}</div>\n`;
    for (const doc of cat.docs) {
      const active = doc === currentDoc ? ' class="active"' : '';
      const title = TITLES[doc] || doc;
      html += `<a href="${doc}.html"${active}>${title}</a>\n`;
    }
  }
  return html;
}

function buildTOC(htmlContent) {
  const headings = [];
  const regex = /<h([2-3])\s+id="([^"]*)"[^>]*>(.*?)<\/h[2-3]>/g;
  let match;
  while ((match = regex.exec(htmlContent)) !== null) {
    headings.push({ level: parseInt(match[1]), id: match[2], text: match[3].replace(/<[^>]*>/g, '') });
  }
  if (headings.length === 0) return '';

  let toc = '<div class="toc"><div class="toc-title">Contents</div>';
  for (const h of headings) {
    const indent = h.level === 3 ? ' style="padding-left:1rem"' : '';
    toc += `<a href="#${h.id}"${indent}>${h.text}</a>`;
  }
  toc += '</div>';
  return toc;
}

function generateHTML(docName, mdContent) {
  const title = TITLES[docName] || docName;

  // heading에 id 부여
  const renderer = new marked.Renderer();
  renderer.heading = function({ tokens, depth }) {
    const text = this.parser.parseInline(tokens);
    const raw = tokens.map(t => t.raw || t.text || '').join('');
    const id = raw.toLowerCase().replace(/[^\w가-힣]+/g, '-').replace(/^-|-$/g, '');
    return `<h${depth} id="${id}">${text}</h${depth}>\n`;
  };

  // .md 링크를 .html로 변환 (같은 디렉토리 문서만)
  renderer.link = function({ href, title: linkTitle, tokens }) {
    const text = this.parser.parseInline(tokens);
    let h = href || '';
    if (h.match(/^[A-Z_]+\.md$/)) {
      h = h.replace(/\.md$/, '.html');
    } else if (h.match(/^[A-Z_]+\.md#/)) {
      h = h.replace(/\.md#/, '.html#');
    }
    const titleAttr = linkTitle ? ` title="${linkTitle}"` : '';
    return `<a href="${h}"${titleAttr}>${text}</a>`;
  };

  // 테이블은 기본 렌더러 사용 (post-processing으로 wrapper 추가)

  // code block — mermaid는 별도 처리
  renderer.code = function({ text, lang }) {
    if (lang === 'mermaid') {
      return `<pre class="mermaid">${text}</pre>\n`;
    }
    const langClass = lang ? ` class="language-${lang}"` : '';
    const escaped = text.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
    return `<pre><code${langClass}>${escaped}</code></pre>\n`;
  };

  let body = marked(mdContent, { renderer });
  // 테이블에 wrapper 추가 (overflow-x 스크롤)
  body = body.replace(/<table>/g, '<div class="table-wrapper"><table>').replace(/<\/table>/g, '</table></div>');
  const sidebar = buildSidebar(docName);
  const toc = buildTOC(body);

  return `<!DOCTYPE html>
<html lang="ko">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>${title} — FASTpass\u00ae SPKD</title>
<style>
:root {
  --primary: #02385e;
  --primary-light: #034b7d;
  --accent: #0ea5e9;
  --bg: #f8fafc;
  --sidebar-bg: #0f172a;
  --sidebar-text: #cbd5e1;
  --sidebar-active: #0ea5e9;
  --card-bg: #ffffff;
  --text: #1e293b;
  --text-secondary: #64748b;
  --border: #e2e8f0;
  --code-bg: #1e293b;
  --code-text: #e2e8f0;
  --table-stripe: #f1f5f9;
  --sidebar-w: 280px;
}
* { margin: 0; padding: 0; box-sizing: border-box; }
body {
  font-family: 'Pretendard', -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
  background: var(--bg);
  color: var(--text);
  line-height: 1.7;
  display: flex;
  min-height: 100vh;
}

/* Sidebar */
.sidebar {
  width: var(--sidebar-w);
  min-height: 100vh;
  background: var(--sidebar-bg);
  padding: 1.5rem 0;
  position: fixed;
  top: 0; left: 0;
  overflow-y: auto;
  z-index: 100;
}
.sidebar-brand {
  padding: 0 1.25rem 1.25rem;
  border-bottom: 1px solid rgba(255,255,255,0.1);
  margin-bottom: 1rem;
}
.sidebar-brand h1 {
  font-size: 1rem;
  font-weight: 700;
  color: #fff;
  letter-spacing: -0.01em;
}
.sidebar-brand .version {
  font-size: 0.75rem;
  color: var(--sidebar-text);
  margin-top: 0.25rem;
}
.nav-category {
  font-size: 0.65rem;
  font-weight: 700;
  text-transform: uppercase;
  letter-spacing: 0.08em;
  color: var(--text-secondary);
  padding: 1rem 1.25rem 0.4rem;
}
.sidebar a {
  display: block;
  padding: 0.35rem 1.25rem 0.35rem 1.5rem;
  color: var(--sidebar-text);
  text-decoration: none;
  font-size: 0.82rem;
  border-left: 3px solid transparent;
  transition: all 0.15s;
}
.sidebar a:hover {
  color: #fff;
  background: rgba(255,255,255,0.05);
}
.sidebar a.active {
  color: var(--sidebar-active);
  border-left-color: var(--sidebar-active);
  background: rgba(14,165,233,0.08);
  font-weight: 600;
}

/* Main */
.main {
  margin-left: var(--sidebar-w);
  flex: 1;
  max-width: 100%;
}
.topbar {
  background: var(--card-bg);
  border-bottom: 1px solid var(--border);
  padding: 0.75rem 2rem;
  position: sticky;
  top: 0;
  z-index: 50;
  display: flex;
  align-items: center;
  gap: 0.75rem;
}
.topbar .breadcrumb {
  font-size: 0.85rem;
  color: var(--text-secondary);
}
.topbar .breadcrumb strong {
  color: var(--text);
}

.content-wrapper {
  display: flex;
}
.content {
  flex: 1;
  min-width: 0;
  padding: 2rem 2.5rem 4rem;
}

/* TOC */
.toc {
  width: 220px;
  position: sticky;
  top: 56px;
  align-self: flex-start;
  padding: 1.5rem 1rem;
  max-height: calc(100vh - 56px);
  overflow-y: auto;
  flex-shrink: 0;
}
.toc-title {
  font-size: 0.7rem;
  font-weight: 700;
  text-transform: uppercase;
  letter-spacing: 0.08em;
  color: var(--text-secondary);
  margin-bottom: 0.75rem;
}
.toc a {
  display: block;
  font-size: 0.78rem;
  color: var(--text-secondary);
  text-decoration: none;
  padding: 0.2rem 0;
  border-left: 2px solid var(--border);
  padding-left: 0.75rem;
  transition: all 0.15s;
}
.toc a:hover {
  color: var(--primary);
  border-left-color: var(--primary);
}

/* Typography */
.content h1 {
  font-size: 2rem;
  font-weight: 800;
  color: var(--primary);
  margin: 0 0 0.5rem;
  letter-spacing: -0.02em;
  line-height: 1.3;
}
.content h2 {
  font-size: 1.4rem;
  font-weight: 700;
  color: var(--primary);
  margin: 2.5rem 0 1rem;
  padding-bottom: 0.4rem;
  border-bottom: 2px solid var(--border);
}
.content h3 {
  font-size: 1.1rem;
  font-weight: 600;
  color: var(--text);
  margin: 1.8rem 0 0.6rem;
}
.content h4 {
  font-size: 0.95rem;
  font-weight: 600;
  color: var(--text-secondary);
  margin: 1.2rem 0 0.4rem;
}
.content p { margin: 0.6rem 0; }
.content ul, .content ol {
  margin: 0.6rem 0;
  padding-left: 1.5rem;
}
.content li { margin: 0.25rem 0; }
.content li > ul, .content li > ol { margin: 0.15rem 0; }
.content strong { font-weight: 600; }
.content a {
  color: var(--accent);
  text-decoration: none;
  border-bottom: 1px solid transparent;
  transition: border-color 0.15s;
}
.content a:hover { border-bottom-color: var(--accent); }
.content hr {
  border: none;
  border-top: 1px solid var(--border);
  margin: 2rem 0;
}
.content blockquote {
  border-left: 4px solid var(--accent);
  background: rgba(14,165,233,0.05);
  padding: 0.75rem 1rem;
  margin: 1rem 0;
  border-radius: 0 6px 6px 0;
  color: var(--text-secondary);
}

/* Code */
.content code {
  font-family: 'JetBrains Mono', 'Fira Code', monospace;
  font-size: 0.85em;
  background: var(--table-stripe);
  padding: 0.15em 0.4em;
  border-radius: 4px;
}
.content pre {
  background: var(--code-bg);
  border-radius: 8px;
  padding: 1rem 1.25rem;
  overflow-x: auto;
  margin: 1rem 0;
  border: 1px solid rgba(255,255,255,0.1);
}
.content pre code {
  background: none;
  padding: 0;
  color: var(--code-text);
  font-size: 0.82rem;
  line-height: 1.6;
}

/* Tables */
.table-wrapper {
  overflow-x: auto;
  margin: 1rem 0;
  border-radius: 8px;
  border: 1px solid var(--border);
}
.content table {
  width: 100%;
  border-collapse: collapse;
  font-size: 0.85rem;
}
.content thead {
  background: var(--primary);
  color: #fff;
}
.content th {
  padding: 0.6rem 0.75rem;
  text-align: left;
  font-weight: 600;
  font-size: 0.8rem;
  white-space: nowrap;
}
.content td {
  padding: 0.5rem 0.75rem;
  border-top: 1px solid var(--border);
}
.content tbody tr:nth-child(even) { background: var(--table-stripe); }
.content tbody tr:hover { background: rgba(14,165,233,0.04); }

/* Mobile menu button */
.menu-toggle {
  display: none;
  background: none;
  border: none;
  color: var(--text);
  cursor: pointer;
  padding: 0.5rem;
  border-radius: 6px;
  line-height: 1;
}
.menu-toggle:hover { background: var(--table-stripe); }
.menu-toggle svg { width: 22px; height: 22px; }

/* Sidebar overlay (mobile) */
.sidebar-overlay {
  display: none;
  position: fixed;
  inset: 0;
  background: rgba(0,0,0,0.5);
  z-index: 90;
  backdrop-filter: blur(2px);
}

/* Sidebar close button */
.sidebar-close {
  display: none;
  position: absolute;
  top: 1rem;
  right: 1rem;
  background: none;
  border: none;
  color: var(--sidebar-text);
  cursor: pointer;
  padding: 0.25rem;
  border-radius: 4px;
}
.sidebar-close:hover { color: #fff; background: rgba(255,255,255,0.1); }
.sidebar-close svg { width: 20px; height: 20px; }

/* ===== Responsive: XL (>1400px) — full layout ===== */
@media (min-width: 1401px) {
  .toc { width: 240px; flex-shrink: 0; }
}

/* ===== Responsive: LG (1024-1400px) — hide TOC ===== */
@media (max-width: 1400px) {
  .toc { display: none; }
  .content { max-width: 100%; }
}

/* ===== Responsive: MD (768-1023px) — collapsible sidebar ===== */
@media (max-width: 1023px) {
  :root { --sidebar-w: 260px; }
  .sidebar {
    transform: translateX(-100%);
    transition: transform 0.25s ease;
    box-shadow: 4px 0 24px rgba(0,0,0,0.3);
  }
  .sidebar.open { transform: translateX(0); }
  .sidebar-overlay.open { display: block; }
  .sidebar-close { display: block; }
  .menu-toggle { display: block; }
  .main { margin-left: 0; }
  .content { padding: 2rem 2rem 4rem; }
  .topbar { padding: 0.75rem 1.5rem; }
}

/* ===== Responsive: SM (480-767px) — mobile ===== */
@media (max-width: 767px) {
  .content {
    padding: 1.25rem 1rem 3rem;
  }
  .content h1 { font-size: 1.5rem; }
  .content h2 { font-size: 1.15rem; margin-top: 2rem; }
  .content h3 { font-size: 1rem; }
  .content pre {
    padding: 0.75rem;
    font-size: 0.78rem;
    border-radius: 6px;
  }
  .topbar { padding: 0.6rem 1rem; }
  .topbar .breadcrumb { font-size: 0.8rem; }
  .table-wrapper { margin: 0.75rem -1rem; border-radius: 0; border-left: none; border-right: none; }
  .content th, .content td { padding: 0.4rem 0.5rem; font-size: 0.78rem; }
  pre.mermaid { padding: 1rem 0.5rem; margin: 1rem -1rem; border-radius: 0; border-left: none; border-right: none; }
}

/* ===== Responsive: XS (<480px) — small phone ===== */
@media (max-width: 479px) {
  .content { padding: 1rem 0.75rem 2rem; }
  .content h1 { font-size: 1.3rem; }
  .content h2 { font-size: 1.05rem; }
  .content code { font-size: 0.78em; }
  .sidebar-brand h1 { font-size: 0.9rem; }
}

/* Print */
@media print {
  .sidebar, .sidebar-overlay, .toc, .topbar, .menu-toggle { display: none !important; }
  .main { margin-left: 0; }
  .content { max-width: 100%; padding: 0; }
  .content h1 { font-size: 1.5rem; }
  .content h2 { font-size: 1.2rem; break-after: avoid; }
  .content pre { white-space: pre-wrap; border: 1px solid #ccc; }
  pre.mermaid { break-inside: avoid; }
}
/* Mermaid diagrams */
pre.mermaid {
  background: linear-gradient(135deg, #0f172a 0%, #1e293b 100%);
  border: 1px solid rgba(14,165,233,0.2);
  border-radius: 12px;
  padding: 2rem 1.5rem;
  text-align: center;
  overflow-x: auto;
  margin: 1.5rem 0;
  box-shadow: 0 4px 24px rgba(0,0,0,0.15);
}
pre.mermaid svg {
  max-width: 100%;
  height: auto;
}
/* Mermaid node overrides */
pre.mermaid .node rect,
pre.mermaid .node polygon,
pre.mermaid .node circle {
  rx: 8px;
  ry: 8px;
}
pre.mermaid .edgeLabel {
  font-size: 13px !important;
}
</style>
</head>
<body>
<div class="sidebar-overlay" id="sidebarOverlay"></div>
<nav class="sidebar" id="sidebar">
  <button class="sidebar-close" id="sidebarClose" aria-label="Close menu">
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M18 6L6 18M6 6l12 12"/></svg>
  </button>
  <div class="sidebar-brand">
    <h1>FASTpass\u00ae SPKD</h1>
    <div class="version">Documentation v2.37.0</div>
  </div>
  ${sidebar}
</nav>
<div class="main">
  <div class="topbar">
    <button class="menu-toggle" id="menuToggle" aria-label="Open menu">
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M3 12h18M3 6h18M3 18h18"/></svg>
    </button>
    <div class="breadcrumb">Docs / <strong>${title}</strong></div>
  </div>
  <div class="content-wrapper">
    <article class="content">
      ${body}
    </article>
    ${toc}
  </div>
</div>
<script src="mermaid.min.js"></script>
<script>
mermaid.initialize({
  startOnLoad: true,
  theme: 'base',
  themeVariables: {
    // Background & text
    background: 'transparent',
    primaryColor: '#1e3a5f',
    primaryTextColor: '#f1f5f9',
    primaryBorderColor: '#38bdf8',
    // Secondary nodes
    secondaryColor: '#0f4c75',
    secondaryTextColor: '#f1f5f9',
    secondaryBorderColor: '#0ea5e9',
    // Tertiary nodes
    tertiaryColor: '#164e63',
    tertiaryTextColor: '#f1f5f9',
    tertiaryBorderColor: '#22d3ee',
    // Lines & labels
    lineColor: '#64748b',
    textColor: '#e2e8f0',
    // Fonts
    fontFamily: '"Pretendard", -apple-system, "Segoe UI", sans-serif',
    fontSize: '16px',
    // Subgraph
    clusterBkg: 'rgba(14,165,233,0.08)',
    clusterBorder: 'rgba(56,189,248,0.3)',
    // Notes
    noteBkgColor: '#1e293b',
    noteTextColor: '#e2e8f0',
    noteBorderColor: '#475569',
    // Sequence diagram
    actorBkg: '#1e3a5f',
    actorBorder: '#38bdf8',
    actorTextColor: '#f1f5f9',
    actorLineColor: '#475569',
    signalColor: '#e2e8f0',
    signalTextColor: '#e2e8f0',
    labelBoxBkgColor: '#1e3a5f',
    labelBoxBorderColor: '#38bdf8',
    labelTextColor: '#f1f5f9',
    loopTextColor: '#94a3b8',
    activationBorderColor: '#38bdf8',
    activationBkgColor: '#0f4c75',
    sequenceNumberColor: '#0f172a',
    // ER diagram
    entityBkg: '#1e3a5f',
    entityBorder: '#38bdf8',
    entityTextColor: '#f1f5f9',
    relationColor: '#64748b',
    relationLabelColor: '#cbd5e1',
    attributeBackgroundColorOdd: '#1e293b',
    attributeBackgroundColorEven: '#0f172a',
    // Edge labels
    edgeLabelBackground: '#1e293b',
  },
  flowchart: {
    curve: 'basis',
    padding: 20,
    nodeSpacing: 50,
    rankSpacing: 60,
    htmlLabels: true,
    useMaxWidth: true,
  },
  sequence: {
    actorFontSize: 14,
    messageFontSize: 14,
    noteFontSize: 13,
    mirrorActors: false,
    bottomMarginAdj: 2,
    useMaxWidth: true,
  },
  er: {
    fontSize: 18,
    useMaxWidth: true,
    layoutDirection: 'TB',
  },
});
</script>
<script>
(function(){
  var btn=document.getElementById('menuToggle'),
      sb=document.getElementById('sidebar'),
      ov=document.getElementById('sidebarOverlay'),
      cl=document.getElementById('sidebarClose');
  function open(){sb.classList.add('open');ov.classList.add('open');document.body.style.overflow='hidden';}
  function close(){sb.classList.remove('open');ov.classList.remove('open');document.body.style.overflow='';}
  if(btn)btn.addEventListener('click',open);
  if(ov)ov.addEventListener('click',close);
  if(cl)cl.addEventListener('click',close);
  sb.querySelectorAll('a').forEach(function(a){a.addEventListener('click',close);});
})();
</script>
</body>
</html>`;
}

// Main
mkdirSync(OUT_DIR, { recursive: true });

const mdFiles = readdirSync(DOCS_DIR)
  .filter(f => f.endsWith('.md'))
  .sort();

console.log(`Converting ${mdFiles.length} markdown files to HTML...`);

for (const file of mdFiles) {
  const docName = basename(file, '.md');
  const mdContent = readFileSync(join(DOCS_DIR, file), 'utf-8');
  const html = generateHTML(docName, mdContent);
  const outFile = join(OUT_DIR, `${docName}.html`);
  writeFileSync(outFile, html, 'utf-8');
  console.log(`  ${file} → html/${docName}.html`);
}

// index.html → README.html 복사
const readmeHtml = readFileSync(join(OUT_DIR, 'README.html'), 'utf-8');
writeFileSync(join(OUT_DIR, 'index.html'), readmeHtml, 'utf-8');

// mermaid.min.js 로컬 복사 (CDN 불필요)
const MERMAID_SRC = join(import.meta.dirname, '../../node_modules/mermaid/dist/mermaid.min.js');
if (existsSync(MERMAID_SRC)) {
  copyFileSync(MERMAID_SRC, join(OUT_DIR, 'mermaid.min.js'));
  console.log('  mermaid.min.js copied to html/');
} else {
  console.warn('  ⚠ mermaid.min.js not found — run: npm install mermaid');
}

console.log(`\nDone! ${mdFiles.length + 1} files written to docs/html/`);
