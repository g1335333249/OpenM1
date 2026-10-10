const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

const html = fs.readFileSync('openm1/recovery_page.html', 'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
new vm.Script(script); // Syntax check of the complete embedded script.
const code = script.split('const apiEndpoints=[', 2)[1].split('// Tabs and polling:', 1)[0];
const elements = new Map();
const makeElement = () => ({value:'', hidden:false, textContent:'', style:{}, children:[],
  addEventListener(){}, setAttribute(){}, scrollIntoView(){}, select(){}, remove(){},
  append(...items){this.children.push(...items)}, replaceChildren(...items){this.children=items}});
const $ = id => {if(!elements.has(id))elements.set(id,makeElement());return elements.get(id)};
$('api-method-filter').value='all';$('api-category-filter').value='all';
let calls=[], mode='json', confirms=0, copied='', fallback=false, switched='';
const document = {createElement:makeElement,body:{appendChild(){}},execCommand(command){assert.equal(command,'copy');fallback=true;copied=lastTextarea.value;return true}};
let lastTextarea;
document.createElement = tag => {const el=makeElement();if(tag==='textarea')lastTextarea=el;return el};
const context = {$,document,navigator:{clipboard:{writeText:async text=>{copied=text}}},
  window:{confirm:()=>{confirms++;return true}},performance:{now:()=>100},Date,
  fetch:async(path,options)=>{calls.push([path,options]);if(mode==='network')throw Error('offline');
    const status=mode==='error'?400:200,raw=mode==='plain'?'plain text':mode==='error'?'{"error":"bad"}':'{"ok":true}';
    return {status,statusText:status===200?'OK':'Bad Request',ok:status===200,
      headers:{get:()=>mode==='plain'?'text/plain':'application/json'},text:async()=>raw}},
  fillGrid:(id,rows)=>{$(id).rows=rows},activateTab:id=>{switched=id},
  clearTimeout(){},setTimeout(){return 1}};
vm.createContext(context);
vm.runInContext('const apiEndpoints=['+code+'\nglobalThis.apiTest={apiEndpoints,selectApi,executeApi,copyApiResponse,formatApiResponse,renderApiList};',context);
const api=context.apiTest;
assert.equal(api.apiEndpoints.length,40);
assert.equal(calls.length,0,'catalog initialization must not fetch APIs');
const endpoint=(method,path)=>api.apiEndpoints.find(e=>e.method===method&&e.path===path);
async function main(){
  api.selectApi(endpoint('GET','/api/system/stats'));
  assert.equal(calls.length,0,'selecting an API must not execute it');
  await api.executeApi();
  assert.equal(calls[0][0],'/api/system/stats');
  assert.equal($('api-response').textContent,'{\n  "ok": true\n}');
  assert.equal($('api-result').hidden,false);
  mode='plain';api.selectApi(endpoint('GET','/api/logs/download'));await api.executeApi();
  assert.equal($('api-response').textContent,'plain text');
  mode='error';api.selectApi(endpoint('GET','/api/health'));await api.executeApi();
  assert.equal($('api-response').textContent,'{\n  "error": "bad"\n}');
  assert.equal($('api-result-info').rows.at(-1)[1],'HTTP 错误');
  mode='network';await api.executeApi();assert.match($('api-response').textContent,/offline/);
  mode='json';api.selectApi(endpoint('GET','/api/logs?after=0'));$('api-after').value='42';await api.executeApi();
  assert.equal(calls.at(-1)[0],'/api/logs?after=42');
  api.selectApi(endpoint('POST','/api/display/brightness'));$('api-body').value='{bad';const before=calls.length;
  await api.executeApi();assert.equal(calls.length,before);assert.match($('api-request-message').textContent,/JSON 语法错误/);
  $('api-body').value='{"brightness":2}';await api.executeApi();
  assert.equal(calls.at(-1)[1].body,'{"brightness":2}');
  api.selectApi(endpoint('POST','/api/reboot'));await api.executeApi();assert.equal(confirms,1);
  assert.equal(calls.at(-1)[1].body,undefined,'no-body POST must not send a body');
  $('api-search').value='亮度';api.renderApiList();assert.match($('api-count').textContent,/2 \/ 40/);
  $('api-search').value='';$('api-method-filter').value='POST';api.renderApiList();
  assert.match($('api-count').textContent,/21 \/ 40/);
  $('api-method-filter').value='all';$('api-category-filter').value='OTA';api.renderApiList();
  assert.match($('api-count').textContent,/4 \/ 40/);
  await api.copyApiResponse();assert.equal(copied,'{\n  "ok": true\n}');
  context.navigator.clipboard={writeText:async()=>{throw Error('unavailable')}};
  await api.copyApiResponse();assert(fallback);assert.equal(copied,'{\n  "ok": true\n}');
  context.document.execCommand=()=>false;await api.copyApiResponse();
  assert.match($('api-copy-message').textContent,/复制失败/);
  api.selectApi(endpoint('POST','/api/ota/upload'));const prior=calls.length;await api.executeApi();
  assert.equal(switched,'update');assert.equal(calls.length,prior,'OTA upload must use existing upgrade flow');
  console.log('API_CONSOLE_PASS: syntax, manual fetch, JSON/plain/400/network, edit, confirm, copy fallback, OTA');
}
main().catch(error=>{console.error(error);process.exitCode=1});
