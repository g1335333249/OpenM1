const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const html=fs.readFileSync('openm1/recovery_page.html','utf8');
const script=html.match(/<script>([\s\S]*?)<\/script>/)[1];
new vm.Script(script);
const part=script.split('let wifiHistory=null;',2)[1].split('async function refreshUart()',1)[0];
const elements=new Map();
let fetches=[],copied='',downloaded='',fallback=false,createdUrl='',tcpOptions=null;
function element(){return {textContent:'',style:{},disabled:false,handlers:{},addEventListener(name,fn){this.handlers[name]=fn},select(){},remove(){},click(){downloaded=this.download}}}
const $=id=>{if(!elements.has(id))elements.set(id,element());return elements.get(id)};
const document={body:{appendChild(){}},createElement:()=>element(),execCommand(name){assert.equal(name,'copy');fallback=true;return true}};
const history={capacity:32,count:1,records:[{uptime_ms:3000,reason:'station_lost',station_phase:'native_reconnect_wait',link_query_result:-1,link_connected:-1,ip_query_result:-32768,ip_valid:-1,rssi:0,wlan_error:-1,wifi_event_code:2,disconnect_count:1,native_reconnect_count:0,station_rearm_count:0,ap_restore_count:0,network_health:'no_wifi',display_target:'blink',pwm_applied:'blink'}]};
const status={verdict:'unsupported',running:false,done:true,api_table_present:true,socket_result:-1,socket_errno:97,parse_result:1,parse_errno:0,aaaa_result:-1,aaaa_errno:0,aaaa_ipv6:false};
const context={$,document,navigator:{clipboard:{writeText:async value=>{copied=value}}},
  jsonGet:async path=>{fetches.push(path);return path==='/api/wifi/history'?history:status},
  fetch:async (path,options)=>{fetches.push(path);if(path.endsWith('/tcp'))tcpOptions=options;return {ok:true,json:async()=>({ok:true})}},
  fillGrid:(id,rows)=>{$(id).rows=rows},JSON,String,Blob,
  URL:{createObjectURL(){createdUrl='blob:test';return createdUrl},revokeObjectURL(){}},
  setTimeout:fn=>{fn();return 1},Promise,confirm:()=>true};
vm.createContext(context);
vm.runInContext('let wifiHistory=null;'+part,context);
assert.deepEqual(fetches,[],'diagnostics must not automatically probe or fetch history');
(async()=>{
  await $('wifi-history-refresh').handlers.click();
  assert.equal(fetches.at(-1),'/api/wifi/history');
  assert.match($('wifi-history-lines').textContent,/station_lost/);
  await $('wifi-history-copy').handlers.click();
  assert.match(copied,/"link_query_result": -1/);
  context.navigator.clipboard=undefined;
  await $('wifi-history-copy').handlers.click();
  assert(fallback,'HTTP fallback must use execCommand');
  await $('wifi-history-export').handlers.click();
  assert.equal(downloaded,'OpenM1-wifi-history.json');
  await $('ipv6-probe').handlers.click();
  assert(fetches.includes('/api/ipv6/probe'));
  assert.match($('ipv6-message').textContent,/检测结束：unsupported/);
  await $('ipv6-tcp-probe').handlers.click();
  assert(fetches.includes('/api/ipv6/probe/tcp'));
  assert.equal(JSON.parse(tcpOptions.body).confirm_ipv6_tcp,true);
  context.navigator.clipboard={writeText:async value=>{copied=value}};
  await $('ipv6-copy').handlers.click();
  assert.match(copied,/"verdict": "unsupported"/);
  console.log('V014_DIAGNOSTICS_WEB_PASS: manual history, copy fallback, export, IPv6 probe');
})().catch(error=>{console.error(error);process.exitCode=1});
