/**
 * 全 UI 界面截图生成器 —— 输出 docs/ui/{en,zh}/NN-name.png
 *
 * 用法：
 *   npm i playwright-core          # 一次性
 *   node tools/screenshots.mjs     # 在仓库根目录运行
 *
 * 依赖本机 Chromium：默认取 PLAYWRIGHT 环境或 /opt/pw-browsers/chromium，
 * 也可用 CHROMIUM=/path/to/chrome node tools/screenshots.mjs 指定。
 */
import { chromium } from 'playwright-core';
import { mkdirSync } from 'fs';
import { fileURLToPath } from 'url';
import path from 'path';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const PROTO = 'file://' + path.join(ROOT, 'prototype', 'index.html');
const EXEC = process.env.CHROMIUM || '/opt/pw-browsers/chromium';

const browser = await chromium.launch({
  executablePath: EXEC,
  args: ['--autoplay-policy=no-user-gesture-required', '--mute-audio'],
});

async function shootLang(lang) {
  const OUT = path.join(ROOT, 'docs', 'ui', lang);
  mkdirSync(OUT, { recursive: true });
  const ctx = await browser.newContext({ viewport: { width: 900, height: 900 } });
  const page = await ctx.newPage();
  page.on('pageerror', (e) => console.log('PAGE ERR:', e.message));
  await page.goto(PROTO + '?lang=' + lang);
  await page.waitForTimeout(700);

  const shot = async (name) => {
    const box = await page.locator('#atomRoot').boundingBox();
    await page.screenshot({ path: path.join(OUT, name + '.png'), clip: box });
    console.log(lang, name);
  };
  const ev = (fn) => page.evaluate(fn);
  const noToast = () => ev(() => $('a-toast').classList.remove('show'));

  /* 空状态首页（注入演示档案前） */
  await ev(() => { S.screen = 'idle'; bump(); });
  await page.waitForTimeout(250);
  await shot('19-home-empty');

  /* 注入演示档案 */
  await ev(() => {
    profile.streak = 6; profile.total = 1284; profile.stars = 27;
    profile.bestSession = 132; profile.bests[1] = 118;
    profile.history = {};
    const counts = [120, 150, 80, 210, 100, 160];
    for (let i = 6; i >= 1; i--)
      profile.history[dayKeyOffset(-i)] = { count: counts[i - 1], ms: 90000 + i * 9000, done: true };
    profile.history[todayKey()] = { count: 128, ms: 96000, done: true };
    saveProfile(); S.screen = 'launch'; bump();
  });
  await page.waitForTimeout(250);
  await shot('01-launch');

  await ev(() => { S.screen = 'idle'; bump(); setCard(0); });
  await page.waitForTimeout(250);
  await shot('02-home');
  await ev(() => setCard(1)); await page.waitForTimeout(350); await shot('03-log-page');
  await ev(() => setCard(2)); await page.waitForTimeout(350); await shot('04-ranking-page');
  await ev(() => setCard(0)); await page.waitForTimeout(300);

  /* 二级页 */
  await ev(() => { renderHistory(); $('a-history').classList.add('show'); });
  await page.waitForTimeout(250); await shot('15-log-overlay');
  await ev(() => { $('a-history').classList.remove('show'); renderABoard(); $('a-board').classList.add('show'); });
  await page.waitForTimeout(250); await shot('16-ranking-overlay');
  await ev(() => { $('a-board').classList.remove('show'); renderBest(); $('a-best').classList.add('show'); });
  await page.waitForTimeout(250); await shot('17-best-records');
  await ev(() => { $('a-best').classList.remove('show'); renderSettings(); $('a-set').classList.add('show'); });
  await page.waitForTimeout(250); await shot('18-settings');
  await ev(() => $('a-set').classList.remove('show'));

  /* 模式选择 → 3·2·1 → BodyPark 发令 */
  await ev(() => openMode());
  await page.waitForTimeout(250); await shot('05-mode-select');
  await ev(() => startSession(50));
  await page.waitForTimeout(350); await shot('06-countdown');
  await page.waitForFunction(() => S.brandFlash === true, null, { timeout: 8000 });
  await page.waitForTimeout(120); await shot('07-go-bodypark');

  /* 训练屏 */
  await page.waitForFunction(() => S.screen === 'jump', null, { timeout: 5000 });
  await page.waitForTimeout(600);
  for (let i = 0; i < 12; i++) { await ev(() => registerJump()); await page.waitForTimeout(300); }
  await noToast(); await page.waitForTimeout(500);
  await shot('08-jumping');
  await ev(() => { registerJump(); $('a-toast').classList.remove('show'); });
  await page.waitForTimeout(90); await noToast();
  await shot('09-jumping-ripple');
  await ev(async () => { while (S.count < 56) { registerJump(); await new Promise((r) => setTimeout(r, 55)); } });
  await page.waitForTimeout(200);
  await ev(() => { registerJump(); $('a-toast').classList.remove('show'); });
  await page.waitForTimeout(100); await noToast();
  await shot('10-jumping-fire');

  /* 暂停遮罩（截完立即恢复，避开 5s 自动结算） */
  await ev(() => pauseSession(true));
  await page.waitForTimeout(250); await shot('11-paused');
  await ev(() => pauseSession(false));
  await page.waitForTimeout(200);

  /* 庆祝 → 结算 → 分享面 */
  await ev(() => endSession());
  await page.waitForTimeout(500); await shot('12-celebrate');
  await page.waitForFunction(() => S.screen === 'result', null, { timeout: 5000 });
  await page.waitForTimeout(350); await shot('13-result');
  await ev(() => { $('a-r-page1').classList.add('hidden'); $('a-r-page2').classList.remove('hidden'); });
  await page.waitForTimeout(250); await shot('14-result-share');

  /* 极限状态：四位数计数降档 */
  await ev(() => { $('a-r-page2').classList.add('hidden'); $('a-r-page1').classList.remove('hidden'); goHome(); });
  await page.waitForTimeout(200);
  await ev(() => { startSession(100); });
  await page.waitForFunction(() => S.screen === 'jump', null, { timeout: 8000 });
  await ev(() => { S.count = 1287; S.goalHit = true; S.elapsedMs = 3540000; S.runSince = Date.now(); bump(); });
  await page.waitForTimeout(500); await noToast(); await page.waitForTimeout(100); await noToast();
  await shot('20-count-4digit');

  await ctx.close();
}

await shootLang('en');
await shootLang('zh');
await browser.close();
console.log('ALL DONE → docs/ui/{en,zh}/');
