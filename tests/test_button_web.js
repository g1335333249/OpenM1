const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const html=fs.readFileSync('openm1/recovery_page.html','utf8');
const script=html.match(/<script>([\s\S]*?)<\/script>/)[1];
new vm.Script(script);
const part=script.split("let lastButtonState='idle',lastButtonTick=0;",2)[1].split('async function refreshUart()',1)[0];
const elements=new Map();
const $=id=>{if(!elements.has(id))elements.set(id,{textContent:''});return elements.get(id)};
let response={factory_reset_state:'waiting_confirmation',confirmation_remaining_ms:12000,
  short_press_frames:1,long_press_frames:1,last_long_press_ms:10000,factory_reset_dry_run:true};
const context={$,
  jsonGet:async path=>{assert.equal(path,'/api/button/status');return response},
  fillGrid:(id,rows)=>{$(id).rows=rows},Date};
vm.createContext(context);
vm.runInContext("let lastButtonState='idle',lastButtonTick=0;"+part+'globalThis.refreshButton=refreshButton;',context);
(async()=>{
  await context.refreshButton();
  assert.match($('button-message').textContent,/15 秒内再次长按/);
  assert($('button-info').rows.some(([name,value])=>name==='模拟模式'&&value.includes('不写配置')));
  response={...response,factory_reset_state:'idle',confirmation_remaining_ms:0};
  await context.refreshButton();assert.match($('button-message').textContent,/已取消/);
  response={...response,factory_reset_state:'reset_completed',dry_run_confirmations:1};
  await context.refreshButton();assert.match($('button-message').textContent,/未修改配置，未重启/);
  console.log('BUTTON_WEB_PASS: waiting window, cancellation, dry-run result');
})().catch(error=>{console.error(error);process.exitCode=1});
