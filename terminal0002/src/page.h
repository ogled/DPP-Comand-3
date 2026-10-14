// The page phones see on the terminal's access point. Self-contained:
// a phone on the access point usually has no internet, so there are no
// web fonts or scripts from elsewhere. Colours follow cosmos1562.space.
#pragma once
#include <pgmspace.h>

static const char PAGE[] PROGMEM = R"html(<!doctype html>
<html lang="ru"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#070b10"><title>Терминал</title>
<style>
:root{--bg:#070b10;--s1:#0b1119;--s2:#101822;--s3:#16202b;--line:rgba(146,178,204,.11);--line2:rgba(146,178,204,.22);
--ink:#dde6ef;--ink2:#97a8b9;--ink3:#6d7e8f;--sig:#29c5e8;--sigd:rgba(41,197,232,.13);--ok:#3ddc97;--okd:rgba(61,220,151,.13);
--warn:#e8a33d;--bad:#ef6461;--r:2px;--mono:ui-monospace,"SF Mono",Menlo,Consolas,monospace}
*{box-sizing:border-box}
[hidden]{display:none!important}
html{color-scheme:dark}
body{margin:0;height:100vh;height:100dvh;display:flex;flex-direction:column;background:var(--bg);color:var(--ink);
font:15px/1.5 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;-webkit-font-smoothing:antialiased}
button,input{font:inherit;color:inherit}
button{cursor:pointer}
:focus-visible{outline:2px solid var(--sig);outline-offset:2px}
.num{font-family:var(--mono);font-variant-numeric:tabular-nums}
.top{display:flex;align-items:center;gap:12px;padding:10px 16px;padding-top:max(10px,env(safe-area-inset-top));border-bottom:1px solid var(--line)}
.who{flex:1;min-width:0}
.who b{display:block;font-size:15px;font-weight:600}
.st{display:flex;flex-wrap:wrap;gap:4px 14px;font-size:12px;color:var(--ink2)}
.st span{display:inline-flex;align-items:center;gap:6px}
.dot{width:7px;height:7px;border-radius:50%;background:var(--ink3);flex:none}
.dot.on{background:var(--ok);box-shadow:0 0 0 3px var(--okd)}
.dot.warn{background:var(--warn)}.dot.bad{background:var(--bad)}
.btn{height:34px;padding:0 12px;border-radius:var(--r);border:1px solid var(--line2);background:none;font-size:13px}
.btn:hover{background:var(--s2)}
.btn.pri{background:var(--sig);border-color:var(--sig);color:#041018;font-weight:600}
.btn.pri:disabled{background:var(--s3);border-color:var(--line);color:var(--ink3);cursor:default}
#log{flex:1;overflow-y:auto;padding:16px;display:flex;flex-direction:column;gap:10px}
.empty{margin:auto;max-width:34ch;text-align:center;color:var(--ink3);font-size:14px}
.m{max-width:min(82%,520px);padding:8px 12px;border:1px solid var(--line);border-radius:var(--r);background:var(--s1);align-self:flex-start}
.m.here{align-self:flex-end;background:var(--s2);border-color:var(--line2)}
.m.site,.m.sat{border-left:2px solid var(--sig)}
.who-said{display:block;font-size:12px;font-weight:600;color:var(--ink2);margin-bottom:2px}
.txt{white-space:pre-wrap;overflow-wrap:anywhere}
.meta{display:flex;justify-content:flex-end;gap:8px;margin-top:2px;font-size:11px;color:var(--ink3)}
.meta .from{color:var(--sig)}
.m .photo{display:block;width:100%;max-width:320px;margin:4px 0 2px;border-radius:var(--r);background:var(--s2)}
.m .save{font-size:12px;color:var(--sig);text-decoration:none}
.sys{align-self:center;max-width:42ch;text-align:center;font-size:12px;color:var(--ink3)}
.under .photo-prog{color:var(--sig)}
.compose{border-top:1px solid var(--line);padding:10px 16px;padding-bottom:max(10px,env(safe-area-inset-bottom))}
.row{display:flex;gap:8px}
/* 16px: below that, iPhones zoom the page in on every tap into a field */
.in{height:40px;padding:0 12px;min-width:0;background:var(--s1);border:1px solid var(--line2);border-radius:var(--r);font-size:16px}
.in:focus{outline:none;border-color:var(--sig)}
.in::placeholder{color:var(--ink3)}
#nick{width:96px;flex:none}#text{flex:1}
.send{width:40px;height:40px;flex:none;display:grid;place-items:center;border:0;border-radius:var(--r);background:var(--sig);color:#041018}
.send:disabled{background:var(--s3);color:var(--ink3);cursor:default}
.under{display:flex;flex-wrap:wrap;justify-content:space-between;gap:4px 12px;margin-top:6px;font-size:12px;color:var(--ink3)}
.under b{color:var(--ink2);font-weight:600}
.under .over{color:var(--bad)}
.sheet{position:fixed;inset:0;z-index:5;display:flex;align-items:flex-end;justify-content:center;background:rgba(0,0,0,.6)}
.panel{width:100%;max-width:460px;max-height:92vh;overflow-y:auto;background:var(--s1);border:1px solid var(--line2);border-bottom:0;
border-radius:var(--r) var(--r) 0 0;padding:16px;padding-bottom:max(18px,env(safe-area-inset-bottom));display:flex;flex-direction:column;gap:14px}
@media(min-width:600px){.sheet{align-items:center}.panel{border-bottom:1px solid var(--line2);border-radius:var(--r)}}
.ph{display:flex;align-items:center;justify-content:space-between}.ph b{font-size:16px}
.x{width:32px;height:32px;border:0;background:none;color:var(--ink2);font-size:18px}
.tabs{display:flex;border:1px solid var(--line2);border-radius:var(--r)}
.tab{flex:1;height:34px;border:0;background:none;color:var(--ink2);font-size:13px}
.tab+.tab{border-left:1px solid var(--line2)}
.tab[aria-selected=true]{background:var(--s3);color:var(--ink)}
.sec{display:flex;flex-direction:column;gap:12px}
.fl{display:flex;flex-direction:column;gap:6px;font-size:12px;color:var(--ink2)}
.fl .in{width:100%;color:var(--ink)}
.hint{font-size:12px;color:var(--ink3)}
.sw{display:flex;align-items:center;justify-content:space-between;gap:12px;font-size:14px}
.sw input{appearance:none;-webkit-appearance:none;width:40px;height:22px;margin:0;flex:none;border-radius:11px;background:var(--s3);position:relative;transition:background .11s}
.sw input::before{content:"";position:absolute;top:3px;left:3px;width:16px;height:16px;border-radius:50%;background:var(--ink2);transition:left .11s}
.sw input:checked{background:var(--sig)}.sw input:checked::before{left:21px;background:#041018}
.card{display:flex;align-items:center;gap:10px;padding:10px 12px;border:1px solid var(--line2);border-radius:var(--r);font-size:14px}
.card small{display:block;font-size:12px;color:var(--ink3)}
.lh{display:flex;align-items:center;justify-content:space-between;font-size:12px;color:var(--ink2)}
.lk{border:0;background:none;padding:0;color:var(--sig);font-size:12px}
#nets{display:flex;flex-direction:column;border-top:1px solid var(--line);max-height:34vh;overflow-y:auto}
.net{display:flex;align-items:center;gap:10px;width:100%;padding:10px 4px;border:0;border-bottom:1px solid var(--line);background:none;text-align:left}
.net span{flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.net[aria-pressed=true]{color:var(--sig)}
.net small{font-size:11px;color:var(--ink3)}
.bars{display:flex;align-items:flex-end;gap:2px;height:12px}.bars i{width:3px;background:var(--s3)}.bars i.on{background:var(--ink2)}
.msg{min-height:18px;font-size:13px;color:var(--ink3)}.msg.e{color:var(--bad)}.msg.k{color:var(--ok)}
</style></head><body>
<header class="top">
  <div class="who"><b id="name">Терминал</b>
    <div class="st"><span><i class="dot on"></i>Радио</span><span><i class="dot" id="sd"></i><span id="ss">…</span></span></div>
  </div>
  <button class="btn" id="open">Настройки</button>
</header>
<main id="log" aria-live="polite"><p class="empty" id="empty">Здесь появятся сообщения радиоканала. Напишите первым: сообщение услышат все терминалы рядом.</p></main>
<form class="compose" id="f">
  <div class="row">
    <input class="in" id="nick" placeholder="Имя" maxlength="12" autocomplete="nickname" aria-label="Ваше имя">
    <input class="in" id="text" placeholder="Сообщение" autocomplete="off" enterkeyhint="send" aria-label="Сообщение">
    <button class="send" id="send" aria-label="Отправить"><svg width="16" height="16" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M2.5 8h10M8.5 3.5L13 8l-4.5 4.5"/></svg></button>
  </div>
  <div class="under"><span id="tip"></span><span class="num" id="bytes"></span></div>
</form>

<div class="sheet" id="sheet" hidden><div class="panel" role="dialog" aria-labelledby="sh">
  <div class="ph"><b id="sh">Настройки</b><button class="x" id="close" aria-label="Закрыть">✕</button></div>
  <div id="login" class="sec">
    <label class="fl">PIN<input class="in" id="pin" type="password" maxlength="16" autocomplete="current-password"></label>
    <div class="msg e" id="lm"></div><button class="btn pri" id="enter">Войти</button>
  </div>
  <div id="admin" class="sec" hidden>
    <div class="tabs" role="tablist">
      <button class="tab" role="tab" aria-selected="true" data-t="0">Терминал</button>
      <button class="tab" role="tab" aria-selected="false" data-t="1">Доступ</button>
    </div>
    <div class="sec">
      <label class="fl">Номер терминала<input class="in num" id="c_terminal" type="number" min="1" max="999" inputmode="numeric"></label>
      <label class="fl">Имя точки доступа<input class="in" id="c_apName" maxlength="32"><span class="hint" id="aph"></span></label>
      <label class="sw">Открытая точка доступа<input id="c_apOpen" type="checkbox"></label>
      <label class="fl" id="appl">Пароль точки доступа<input class="in" id="c_apPass" type="password" maxlength="64"><span class="hint">От 8 символов. Пусто: без изменений</span></label>
      <button class="btn pri sv">Сохранить</button><div class="msg"></div>
    </div>
    <div class="sec" hidden>
      <label class="fl">Новый PIN<input class="in" id="c_pin" type="password" maxlength="16" autocomplete="new-password"><span class="hint">Пусто: без изменений</span></label>
      <label class="sw">Настройки без PIN<input id="c_noPin" type="checkbox"></label>
      <button class="btn pri sv">Сохранить</button><div class="msg"></div>
      <button class="btn" id="logout">Выйти из настроек</button>
    </div>
  </div>
</div></div>

<script>
const $=id=>document.getElementById(id),log=$('log'),enc=new TextEncoder(),MAX=240;
let after=0,pin='',st={},seen={},sel=null,watch=0;
try{$('nick').value=localStorage.getItem('nick')||''}catch(e){}

// --- channel ---
const SAID=/^([^:\n]{1,12}): ([\s\S]+)$/;
function hm(m){const s=m.at||(seen[m.id]??=Date.now()/1000);return new Date(s*1e3).toLocaleTimeString('ru-RU',{hour:'2-digit',minute:'2-digit'})}
// "5" -> "#5" (a terminal), "@anton" and "#48213" (people on the site) as they are, "*" -> nobody in particular
function who(x){return !x||x=='*'?'':/^\d+$/.test(x)?'#'+x:x}
function tag(text,cls){const s=document.createElement('span');s.className=cls||'';s.textContent=text;return s}
function add(m){
  $('empty')?.remove();
  // A note from the terminal itself: about /cam, and the like.
  if(m.o=='sys'){const n=tag(m.t,'sys');log.append(n);log.scrollTop=log.scrollHeight;return}
  const d=document.createElement('div');d.className='m '+m.o;
  const p=SAID.exec(m.t);
  // The speaker: the nick in the text, else whoever sent it (a site person has no nick).
  const speaker=p?p[1]:(m.f&&!/^\d+$/.test(m.f)?m.f:'');
  if(speaker)d.append(tag(speaker,'who-said'));
  const t=document.createElement('div');t.className='txt';t.textContent=p?p[2]:m.t;d.append(t);
  if(m.img){const src='/api/image?id='+m.img,im=document.createElement('img');im.className='photo';im.src=src;im.alt='Снимок со спутника';
    const a=document.createElement('a');a.className='save';a.href=src;a.download='sputnik_'+m.img+'.jpg';a.textContent='Сохранить снимок';d.append(im,a)}
  const meta=document.createElement('div');meta.className='meta';
  if(m.o=='site')meta.append(tag('с сайта','from'));
  if(m.o=='sat')meta.append(tag('через спутник','from'));
  // Another terminal heard here, or a dumped line: say which terminal it came from.
  if((m.o=='air'||m.o=='sat')&&/^\d+$/.test(m.f||''))meta.append(tag('с терминала #'+m.f));
  if(m.o!='sat'&&who(m.to)&&m.to!=String(st.terminal))meta.append(tag('→ '+who(m.to)));
  meta.append(tag(hm(m),'num'));d.append(meta);
  const near=log.scrollHeight-log.scrollTop-log.clientHeight<80;log.append(d);if(near||m.o=='here')log.scrollTop=log.scrollHeight}
async function lines(){try{const r=await fetch('/api/lines?after='+after);for(const m of await r.json()){add(m);after=m.id}}catch(e){}setTimeout(lines,1000)}

// --- status ---
const SITE={online:['on','Сайт на связи'],key:['bad','Сайт не принял ключ'],trouble:['warn','Сайт не отвечает'],nowifi:['','Без интернета'],off:['','Без интернета']};
async function state(){try{st=await(await fetch('/api/state')).json();
  const n=String(st.terminal).padStart(3,'0');$('name').textContent='Терминал '+n;document.title=$('name').textContent;
  const [c,l]=SITE[st.site]||SITE.off;$('sd').className='dot '+c;$('ss').textContent=l;count()}catch(e){}}
// A photo on its way: poll faster so the progress moves.
setInterval(()=>{if(st.photo)state()},1000);
// The log starts once the terminal's own number is known: it decides which lines say "→ #n".
setInterval(state,3000);state().then(lines);

// --- composing ---
function line(){const n=$('nick').value.trim(),v=$('text').value.replace(/\s+/g,' ').trim();return v?(n?n+': ':'')+v:''}
function count(){const l=line(),b=enc.encode(l).length;
  $('bytes').textContent=b+' / '+MAX;$('bytes').className='num'+(b>MAX?' over':'');$('send').disabled=!l||b>MAX;
  $('tip').innerHTML=st.photo
    ?'<span class="photo-prog">Принимаем снимок: '+st.photo.got+' из '+st.photo.total+'</span>'
    :'<b>@позывной</b> или <b>#номер</b> в начале: лично. <b>/cam</b>: снимок со спутника'}
$('text').oninput=count;$('nick').oninput=count;count();
$('f').onsubmit=async e=>{e.preventDefault();const l=line();if(!l||enc.encode(l).length>MAX)return;
  const n=$('nick').value.trim(),v=$('text').value.replace(/\s+/g,' ').trim();
  try{localStorage.setItem('nick',n)}catch(e){}
  $('send').disabled=true;
  try{const r=await fetch('/api/send',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'n='+encodeURIComponent(n)+'&t='+encodeURIComponent(v)});
    if(r.ok)$('text').value=''}catch(e){}
  count();$('text').focus()};

// --- settings ---
const C=k=>$('c_'+k);
function post(u,o){return fetch(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
  body:Object.entries(Object.assign({k:pin},o||{})).map(([a,v])=>a+'='+encodeURIComponent(v)).join('&')})}
function say(el,t,c){el.textContent=t;el.className='msg'+(c?' '+c:'')}
function showLogin(err){$('login').hidden=false;$('admin').hidden=true;say($('lm'),err||'','e');$('pin').value='';setTimeout(()=>$('pin').focus(),50)}
async function info(fill){let r;try{r=await post('/api/admin/info')}catch(e){return}
  if(r.status==403){pin='';showLogin(fill?'Неверный PIN':'Нужно войти');return}
  const c=await r.json();
  if(!fill)return;
  $('login').hidden=true;$('admin').hidden=false;
  C('terminal').value=c.terminal;
  C('apName').value=c.apName;C('apName').placeholder=c.apDefault;$('aph').textContent='Пусто: '+c.apDefault;
  C('apOpen').checked=c.apOpen;C('apPass').value='';C('pin').value='';C('noPin').checked=!c.pin;apOpen()}
function apOpen(){$('appl').hidden=C('apOpen').checked}C('apOpen').onchange=apOpen;
function open_(){$('sheet').hidden=false;if(pin||!st.pin)info(1);else showLogin();clearInterval(watch);watch=setInterval(()=>{if(!$('admin').hidden)info(0)},3000)}
function close_(){$('sheet').hidden=true;clearInterval(watch)}
$('open').onclick=open_;$('close').onclick=close_;$('sheet').onclick=e=>{if(e.target==$('sheet'))close_()};
document.addEventListener('keydown',e=>{if(e.key=='Escape')close_()});
$('enter').onclick=()=>{pin=$('pin').value;info(1)};$('pin').onkeydown=e=>{if(e.key=='Enter')$('enter').click()};
$('logout').onclick=()=>{pin='';showLogin()};
document.querySelectorAll('.tab').forEach(b=>b.onclick=()=>{document.querySelectorAll('.tab').forEach(x=>x.setAttribute('aria-selected',x==b));
  document.querySelectorAll('#admin>.sec').forEach((x,i)=>x.hidden=i!=b.dataset.t)});
document.querySelectorAll('.sv').forEach(b=>b.onclick=async()=>{const m=b.nextElementSibling;b.disabled=true;
  const o={terminal:C('terminal').value,apName:C('apName').value,apPass:C('apPass').value,pin:C('pin').value,
    apOpen:C('apOpen').checked?1:0,noPin:C('noPin').checked?1:0};
  try{const r=await post('/api/admin/save',o),x=await r.text();
    if(r.ok){if(o.noPin==1)pin='';else if(o.pin)pin=o.pin;
      say(m,x=='ap'?'Сохранено. Точка доступа перезапускается: подключитесь к ней снова.':'Сохранено','k');
      C('apPass').value='';C('pin').value='';info(1);state()}
    else if(r.status==403){pin='';showLogin('Нужно войти')}else say(m,x,'e')}catch(e){say(m,'Нет связи с терминалом','e')}
  b.disabled=false});
</script></body></html>)html";
