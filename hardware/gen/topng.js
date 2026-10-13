const { chromium } = require('playwright');
const fs = require('fs');
(async () => {
  const [,, svgf, png, pdf] = process.argv;
  const svg = fs.readFileSync(svgf, 'utf8');
  const b = await chromium.launch();
  const p = await b.newPage({ viewport: { width: 4200, height: 2970 } });
  await p.setContent(`<html><head><style>html,body{margin:0}svg{width:4200px;height:2970px;display:block}</style></head><body>${svg}</body></html>`);
  await p.screenshot({ path: png });
  if (pdf) {
    const q = await b.newPage();
    await q.setContent(`<html><head><style>@page{size:420mm 297mm;margin:0}html,body{margin:0}svg{width:420mm;height:297mm;display:block}</style></head><body>${svg}</body></html>`);
    await q.pdf({ path: pdf, width: '420mm', height: '297mm', printBackground: true, pageRanges: '1' });
  }
  await b.close();
})();
