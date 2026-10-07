const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

const page = fs.readFileSync('openm1/recovery_page.html', 'utf8');
const handler = page.split('\n').find(line => line.startsWith("$('log-copy').addEventListener"));
assert(handler);
const fullLog = 'OpenM1 v0.6.8\n[000001][INFO][BOOT] start\n[000002][WARN][WIFI] retry\n';

async function run(mode) {
  let click, copied, appended = false, selected = false, removed = false, copyCalls = 0;
  const button = { textContent: '复制日志', addEventListener(_event, fn) { click = fn; } };
  const field = { value: '', style: {}, select() { selected = true; }, remove() { removed = true; } };
  const context = {
    $: id => { assert.equal(id, 'log-copy'); return button; },
    fetch: async path => {
      assert.equal(path, '/api/logs/download');
      if (mode === 'fetch_error') return { ok: false };
      return { ok: true, text: async () => fullLog };
    },
    navigator: mode === 'clipboard' ? { clipboard: { writeText: async text => { copied = text; } } } : {},
    document: {
      createElement(tag) { assert.equal(tag, 'textarea'); return field; },
      body: { appendChild(value) { assert.equal(value, field); appended = true; } },
      execCommand(command) { assert.equal(command, 'copy'); copyCalls++; copied = field.value; return true; }
    },
    clearTimeout() {}, setTimeout() { return 1; }
  };
  vm.runInNewContext('let logCopyTimer;\n' + handler, context);
  await click();
  return { button, copied, appended, selected, removed, copyCalls };
}

(async () => {
  const modern = await run('clipboard');
  assert.equal(modern.copied, fullLog);
  assert.equal(modern.copyCalls, 0);
  assert.equal(modern.button.textContent, '日志已复制');
  const fallback = await run('fallback');
  assert.equal(fallback.copied, fullLog);
  assert(fallback.appended && fallback.selected && fallback.removed);
  assert.equal(fallback.copyCalls, 1);
  assert.equal(fallback.button.textContent, '日志已复制');
  const failed = await run('fetch_error');
  assert.equal(failed.button.textContent, '复制失败，请使用下载日志');
  console.log('PASS full RAM log copy: Clipboard API, HTTP fallback, error message');
})().catch(error => { console.error(error); process.exitCode = 1; });
