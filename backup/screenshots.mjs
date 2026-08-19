/**
 * 全 UI 界面截图生成器 —— 输出 docs-and-demo/ui/{en,zh}/NN-name.png
 *
 * 用法：
 *   npm i playwright-core             # 一次性
 *   node backup/screenshots.mjs       # 在仓库根目录运行
 *
 * 依赖本机 Chromium：默认取 /opt/pw-browsers/chromium，
 * 也可用 CHROMIUM=/path/to/chrome node backup/screenshots.mjs 指定。
 */
import { chromium } from 'playwright-core';
import { mkdirSync } from 'fs';
import { fileURLToPath } from 'url';
import path from 'path';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const PROTO = 'file://' + path.join(ROOT, 'docs-and-demo', 'demo.html');
const EXEC = process.env.CHROMIUM || '/opt/pw-browsers/chromium';

const browser = await chromium.launch({
  executablePath: EXEC,
  args: ['--autoplay-policy=no-user-gesture-required', '--mute-audio'],
});

async function shootLang(lang) {
  const OUT = path.join(ROOT, 'docs-and-demo', 'ui', lang);
  mkdirSync(OUT, { recursive: true });
  const ctx = await browser.newContext({ viewport: { width: 900, height: 900 } });
  const page = await ctx.newPage();
  page.on('pageerror', (e) => console.log('PAGE ERR:', e.message));
  await page.goto(PROTO + '?app=1&lang=' + lang);   /* app=1 = 纯 Demo(隐藏手册外壳) */
  await page.waitForTimeout(700);

  const shot = async (name) => {
    const box = await page.locator('#atomRoot').boundingBox();
    await page.screenshot({ path: path.join(OUT, name + '.png'), clip: box });
    console.log(lang, name);
  };
  const ev = (fn) => page.evaluate(fn);
  const noToast = () => ev(() => $('a-toast').classList.remove('show'));

  /* 空状态首页（注入演示档案前;今日未跳 = 邀请行） */
  await ev(() => { S.screen = 'idle'; bump(); });
  await page.waitForTimeout(250);
  await shot('21-home-empty');

  /* 注入演示档案 */
  await ev(() => {
    profile.streak = 6; profile.maxStreak = 12; profile.total = 1284; profile.stars = 27;
    profile.bestSession = 132; profile.bests[1] = 118;
    profile.history = {};
    const counts = [120, 150, 80, 210, 100, 160];
    for (let i = 6; i >= 1; i--)
      profile.history[dayKeyOffset(-i)] = { count: counts[i - 1], n: 1 + (i % 3), best: 90, ms: 90000 + i * 9000, done: true };
    profile.history[todayKey()] = { count: 128, n: 2, best: 96, ms: 96000, done: true };
    saveProfile(); S.screen = 'launch'; bump();
  });
  await page.waitForTimeout(250);
  await shot('01-launch');

  /* 首页卡组 4 页 */
  await ev(() => { S.screen = 'idle'; bump(); setCard(0); });
  await page.waitForTimeout(250);
  await shot('02-home');
  await ev(() => setCard(1)); await page.waitForTimeout(350); await shot('03-log-page');
  await ev(() => setCard(2)); await page.waitForTimeout(350); await shot('04-ranking-page');
  await ev(() => setCard(3)); await page.waitForTimeout(350); await shot('05-best-page');   /* 改版恢复:Page4 = 最佳战绩速览 */
  await ev(() => setCard(0)); await page.waitForTimeout(300);

  /* 二级页 */
  await ev(() => { renderHistory(); $('a-history').classList.add('show'); });
  await page.waitForTimeout(250); await shot('15-log-overlay');
  /* 追加记录:打卡记录页右下「补记」进入 */
  await ev(() => $('a-hist-add').click());
  await page.waitForTimeout(250); await shot('23-add-record');
  await ev(() => $('a-add').classList.remove('show'));
  /* 日卡:点今天那行 */
  await ev(() => document.querySelector('#a-history-list .hrow').click());
  await page.waitForTimeout(250); await shot('16-day-card');
  await ev(() => { $('a-daycard').classList.remove('show'); $('a-history').classList.remove('show'); });
  await ev(() => { renderABoard(); $('a-board').classList.add('show'); });
  await page.waitForTimeout(250); await shot('17-ranking-overlay');
  /* 三榜 Page View:滑到第二榜(1 分钟) */
  await ev(() => setBoardPage(1));
  await page.waitForTimeout(350); await shot('24-ranking-timed');
  await ev(() => { $('a-board').classList.remove('show'); renderBest(); $('a-best').classList.add('show'); });
  await page.waitForTimeout(250); await shot('18-best-records');
  await ev(() => { $('a-best').classList.remove('show'); renderSettings(); $('a-set').classList.add('show'); });
  await page.waitForTimeout(250); await shot('19-settings');
  await ev(() => $('a-set').classList.remove('show'));

  /* 模式选择 → 动作要领幻灯片 → 3·2·1 → BodyPark 发令 */
  await ev(() => openMode());
  await page.waitForTimeout(250); await shot('06-mode-select');
  await ev(() => $('a-mode-tips').click());
  await page.waitForTimeout(250); await shot('20-howto-slide');
  await ev(() => $('a-tips').classList.remove('show'));
  await ev(() => startSession(50));
  await page.waitForTimeout(350); await shot('07-countdown');
  await page.waitForFunction(() => S.brandFlash === true, null, { timeout: 8000 });
  await page.waitForTimeout(120); await shot('08-go-bodypark');

  /* 训练屏 */
  await page.waitForFunction(() => S.screen === 'jump', null, { timeout: 5000 });
  await page.waitForTimeout(600);
  for (let i = 0; i < 12; i++) { await ev(() => registerJump()); await page.waitForTimeout(300); }
  await noToast(); await page.waitForTimeout(500);
  await shot('09-jumping');
  await ev(() => { registerJump(); $('a-toast').classList.remove('show'); });
  await page.waitForTimeout(90); await noToast();
  await shot('10-jumping-ripple');
  await ev(async () => { while (S.count < 56) { registerJump(); await new Promise((r) => setTimeout(r, 55)); } });
  await page.waitForTimeout(200);
  await ev(() => { registerJump(); $('a-toast').classList.remove('show'); });
  await page.waitForTimeout(100); await noToast();
  await shot('11-jumping-fire');

  /* 暂停遮罩（出框立即暂停,3 分钟未回自动结算） */
  await ev(() => pauseSession(true));
  await page.waitForTimeout(250); await shot('12-paused');
  await ev(() => pauseSession(false));
  await page.waitForTimeout(200);

  /* 结算(8-18):默认直进结果页;点「完成」后弹新纪录恭喜帧 */
  await ev(() => { profile.bestSession = 45; S.pbAtStart = 45; saveProfile(); });   /* 让本轮成为新纪录,演示恭喜帧 */
  await ev(() => endSession());
  await page.waitForFunction(() => S.screen === 'result', null, { timeout: 5000 });
  await page.waitForTimeout(350); await shot('14-result');
  await ev(() => document.querySelector('#a-result [data-act="home"]').click());
  await page.waitForTimeout(750); await shot('13-celebrate');   /* 蓄力 260ms 后进入爆发帧 */
  await page.waitForFunction(() => S.screen === 'idle', null, { timeout: 5000 });

  /* 极限状态：四位数计数降档（时长 12min,远离 30min 上限与预告） */
  await page.waitForTimeout(200);
  await ev(() => { startSession(100); });
  await page.waitForFunction(() => S.screen === 'jump', null, { timeout: 8000 });
  await ev(() => { S.count = 1287; S.goalHit = true; S.elapsedMs = 720000; S.runSince = Date.now(); bump(); });
  await page.waitForTimeout(500); await noToast(); await page.waitForTimeout(100); await noToast();
  await shot('22-count-4digit');
  await ev(() => endSession());

  await ctx.close();
}

await shootLang('en');
await shootLang('zh');
await browser.close();
console.log('ALL DONE → docs-and-demo/ui/{en,zh}/');
