#include "app_web.h"

#include "app.h"
#include "app_i18n.h"
#include "app_logic.h"
#include "app_notif.h"
#include "app_notif_store.h"
#include "app_net.h"
#include "app_tone.h"
#include "app_prefs.h"
#include "app_time.h"
#include "bsp_ble.h"
#include "bsp_wifi.h"
#include "qrcode.h"
#include "ui_pixel.h"

#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "lwip/sockets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "app_web";

static const char PAGE[] =
    "<!doctype html><html lang=zh-CN><meta charset=utf-8>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<title>Passport</title>"
    "<style>"
    "*{box-sizing:border-box}"
    "body{margin:0;background:#f4f5f7;color:#202124;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI','Microsoft YaHei',sans-serif}"
    "main{max-width:520px;margin:24px auto;padding:22px 18px;background:#fff;"
    "border-radius:20px;box-shadow:0 8px 28px rgba(30,35,40,.1)}"
    "h1{margin:0 0 8px;font-size:22px;font-weight:700}"
    "p{margin:0 0 12px;color:#70757a;font-size:14px;line-height:1.6}"
    "textarea{width:100%;min-height:140px;font-size:16px;padding:12px;"
    "border:1px solid #d9dce1;background:#fff;color:#202124;"
    "border-radius:12px;box-sizing:border-box;outline:none}"
    "textarea:focus{border-color:#cf3043;box-shadow:0 0 0 3px rgba(207,48,67,.1)}"
    "button{width:100%;margin-top:12px;height:50px;font-size:16px;"
    "font-weight:700;background:#c81d31;color:#fff;"
    "border:0;border-radius:13px;box-shadow:0 6px 16px rgba(200,29,49,.25)}"
    "#ok{min-height:1.4em;color:#c81d31}"
    "</style>"
    "<main><h1>Passport</h1><p id=st></p>"
    "<form method=post action=/t>"
    "<textarea name=t id=t></textarea>"
    "<button type=submit>Send</button></form>"
    "<p id=ok></p>"
"</main>"
    "<script>"
    "let T={ok:'Sent',fail:'Failed'};"
    "const f=document.querySelector('form');"
    "f.onsubmit=async e=>{"
    "e.preventDefault();"
    "const t=document.getElementById('t').value;"
    "try{"
    "const r=await fetch('/t',{method:'POST',"
    "headers:{'Content-Type':'text/plain;charset=utf-8'},body:t});"
    "document.getElementById('ok').textContent=r.ok?T.ok:T.fail;"
    "}catch(err){document.getElementById('ok').textContent=T.fail;}"
    "};"
    "async function st(){try{"
    "const j=await(await fetch('/s')).json();"
    "document.documentElement.lang=j.lang||'en';"
    "T.ok=j.ok;T.fail=j.fail;"
    "document.querySelector('button').textContent=j.send;"
    "document.getElementById('t').placeholder=j.ph;"
    "document.getElementById('st').textContent=j.field?"
    "j.busy.replace('%s',j.field):j.idle;"
    "}catch(e){}}"
    "st();setInterval(st,2000);"
    "</script>";

static const char RULES_PAGE[] =
    "<!doctype html><html lang=zh-CN><meta charset=utf-8>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<title>Rules</title>"
    "<style>"
    "*{box-sizing:border-box}"
    "body{margin:0;background:#f4f5f7;color:#202124;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI','Microsoft YaHei',sans-serif}"
    "main{max-width:520px;margin:16px auto;padding:22px 18px;background:#fff;"
    "border-radius:20px;box-shadow:0 8px 28px rgba(30,35,40,.1)}"
    "h1{margin:0 0 8px;font-size:20px;font-weight:700}"
    "p{margin:0 0 10px;color:#70757a;font-size:14px}"
    ".c{background:#f8f9fa;padding:12px;margin:0 0 8px;border-radius:12px;border:1px solid #e3e5e8}"
    "input,select{width:100%;height:48px;padding:0 13px;margin:4px 0;font-size:15px;"
    "background:#fff;color:#202124;border:1px solid #d9dce1;"
    "border-radius:12px;box-sizing:border-box;outline:none}"
    "input:focus,select:focus{border-color:#cf3043;box-shadow:0 0 0 3px rgba(207,48,67,.1)}"
    "button{width:100%;margin-top:8px;height:50px;font-size:16px;"
    "font-weight:700;background:#c81d31;color:#fff;border:0;border-radius:13px;"
    "box-shadow:0 6px 16px rgba(200,29,49,.25)}"
    ".c>button{width:auto;height:auto;padding:6px 12px;font-size:14px;margin:4px 8px 0 0;background:#f3f4f6;color:#202124;box-shadow:none}"
    ".hd{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-bottom:6px}"
    ".hd input,.hd select{flex:1;min-width:36%;margin:0;height:48px}"
    ".hd button,.del{width:auto;height:auto;margin:0;padding:8px 12px;font-size:14px;background:#fff5f6;color:#c81d31;box-shadow:none}"
    ".addc{width:100%;margin:10px 0 0;height:auto;padding:10px;background:#f3f4f6;color:#202124;box-shadow:none}"
    ".row{display:flex;flex-wrap:wrap;gap:4px;align-items:center;margin-top:6px}"
    ".row select,.row input{flex:1;min-width:28%;margin:0;height:40px}"
    ".row .j{flex:1 1 100%}"
    ".row:first-child .j{display:none}"
    ".row button{width:auto;height:auto;margin:0;padding:8px 10px;font-size:14px;background:#f3f4f6;color:#202124;box-shadow:none}"
    "#ok{min-height:1.4em;color:#c81d31}"
    "</style>"
    "<main><h1 id=h>Rules</h1><div id=list></div>"
    "<button type=button id=add>+</button>"
    "<p id=dl></p><select id=def></select>"
    "<button type=button id=sv>Save</button><p id=ok></p></main>"
    "<script>"
    "let T={ok:'Sent',fail:'Failed',s:['0','1','2','3'],nm:'',f:[],o:[],j:[],ac:'+',max:8},L=[];"
    "const FK=['any','title','sub','msg','app','name','cat'];"
    "const E=s=>String(s||'').replace(/[&<>\"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;'}[c]));"
    "const S=(a,v)=>(a||[]).map((l,i)=>`<option value=${i}${i==v?' selected':''}>${E(l)}</option>`).join('');"
    "const O=(v,id)=>{const h=S(T.s,v);if(id)document.getElementById(id).innerHTML=h;return h};"
    "function term(c){"
    "const f=FK[c.f|0]||'any';if((c.o|0)==5)return f+':';"
    "let v=String(c.v||'').replace(/[&|]/g,'');"
    "if((c.o|0)==4)return f+':='+v;"
    "if((c.o|0)==2)return f+':'+v+(v.indexOf('*')>=0?'':'*');"
    "if((c.o|0)==3)return f+':'+(v[0]=='*'?'':'*')+v;"
    "return ((c.o|0)==1?'!':'')+f+':'+v}"
    "function pack(cs){let e='';(cs||[]).forEach(c=>{"
    "let t=term(c);if(!t)return;if(!e){e=t;return}"
    "const j=['&','|','&!','|!'][c.j|0]||'&';"
    "if((c.j==2||c.j==3)&&t[0]=='!')t=t.slice(1);e+=j+t});return e.slice(0,80)}"
    "function parse(e){const out=[];if(!e){out.push({f:0,o:0,v:'',j:0});return out}"
    "let i=0,first=1;while(i<e.length){let j=0;"
    "if(!first){if(e.startsWith('&!',i)){j=2;i+=2}else if(e.startsWith('|!',i)){j=3;i+=2}"
    "else if(e[i]=='&'){j=0;i++}else if(e[i]=='|'){j=1;i++}}first=0;"
    "let neg=0;if(e[i]=='!'){neg=1;i++}"
    "let amp=e.indexOf('&',i),bar=e.indexOf('|',i),nxt=e.length;"
    "if(amp>=0&&amp<nxt)nxt=amp;if(bar>=0&&bar<nxt)nxt=bar;"
    "const c=e.indexOf(':',i);let f=0,v='';"
    "if(c>=0&&c<nxt){const k=e.slice(i,c);const fi=FK.indexOf(k);f=fi<0?0:fi;v=e.slice(c+1,nxt)}"
    "else v=e.slice(i,nxt);i=nxt;"
    "if(v[0]=='\"'&&v.slice(-1)=='\"')v=v.slice(1,-1);"
    "let o=neg?1:0;if(!v)o=5;else if(v[0]=='='){o=4;v=v.slice(1)}"
    "else if(v[0]=='*'&&v.slice(-1)!='*'){o=3;v=v.slice(1)}"
    "else if(v.slice(-1)=='*'&&v[0]!='*'){o=2;v=v.slice(0,-1)}"
    "out.push({f,o,v,j})}if(!out.length)out.push({f:0,o:0,v:'',j:0});return out}"
    "function draw(){"
    "document.getElementById('list').innerHTML=L.map((r,i)=>"
    "`<div class=c><div class=hd>`"
    "+`<input data-k=n maxlength=16 placeholder=\"${E(T.nm)}\" value=\"${E(r.n)}\">`"
    "+`<select data-k=p>${O(r.p|0)}</select>`"
    "+`<button type=button class=del data-d=${i}>删除规则</button></div><div class=cs>`"
    "+(r.cs||[]).map((c,k)=>`<div class=row>`"
    "+`<select class=j data-k=j>${S(T.j,c.j|0)}</select>`"
    "+`<select data-k=f>${S(T.f,c.f|0)}</select>`"
    "+`<select data-k=o>${S(T.o,c.o|0)}</select>`"
    "+`<input data-k=v maxlength=40 value=\"${E(c.v)}\"${(c.o|0)==5?' style=display:none':''}>`"
    "+`<button type=button class=rm data-x=${i} data-y=${k}>移除条件</button></div>`).join('')"
    "+`</div><button type=button class=addc data-a=${i}>${E(T.ac)}</button></div>`).join('')}"
    "function read(){L=[...document.querySelectorAll('#list>.c')].map(c=>{"
    "const cs=[...c.querySelectorAll('.row')].map(r=>{"
    "const j=r.querySelector('[data-k=j]');"
    "return{f:+r.querySelector('[data-k=f]').value,o:+r.querySelector('[data-k=o]').value,"
    "v:r.querySelector('[data-k=v]').value,j:+(j?j.value:0)}});"
    "return{n:c.querySelector('[data-k=n]').value,p:+c.querySelector('[data-k=p]').value,cs}})}"
    "document.getElementById('list').onclick=e=>{"
    "const t=e.target,d=t.getAttribute('data-d'),a=t.getAttribute('data-a'),x=t.getAttribute('data-x');"
    "if(d==null&&a==null&&x==null)return;read();"
    "if(d!=null)L.splice(+d,1);"
    "else if(a!=null&&L[+a])L[+a].cs.push({f:0,o:0,v:'',j:0});"
    "else if(x!=null){const r=L[+x];if(r){r.cs.splice(+t.getAttribute('data-y'),1);"
    "if(!r.cs.length)r.cs.push({f:0,o:0,v:'',j:0})}}draw()};"
    "document.getElementById('list').onchange=e=>{"
    "if(e.target.getAttribute('data-k')!='o')return;"
    "const v=e.target.parentElement.querySelector('[data-k=v]');"
    "if(v)v.style.display=e.target.value=='5'?'none':''};"
    "document.getElementById('add').onclick=()=>{read();if(L.length>=T.max)return;"
    "L.push({n:'',p:1,cs:[{f:0,o:0,v:'',j:0}]});draw()};"
    "document.getElementById('sv').onclick=async()=>{read();"
    "const r=L.map(x=>({n:x.n,t:pack(x.cs),p:x.p}));"
    "try{const q=await fetch('/r',{method:'POST',"
    "headers:{'Content-Type':'application/json'},"
    "body:JSON.stringify({d:+document.getElementById('def').value,r})});"
    "document.getElementById('ok').textContent=q.ok?T.ok:T.fail}"
    "catch(e){document.getElementById('ok').textContent=T.fail}};"
    "async function st(){try{const j=await(await fetch('/s')).json();"
    "document.documentElement.lang=j.lang||'en';"
    "if(j.mode!='rules')return;"
    "T.ok=j.ok;T.fail=j.fail;T.s=[j.s0,j.s1,j.s2,j.s3];T.nm=j.nm;T.max=j.max||8;"
    "T.f=j.f||[];T.o=j.o||[];T.j=j.j||[];T.ac=j.ac||'+';"
    "document.getElementById('h').textContent=j.title;"
    "document.getElementById('add').textContent=j.add;"
    "document.getElementById('sv').textContent=j.send;"
    "document.getElementById('dl').textContent=j.def;"
    "O(+document.getElementById('def').value,'def')}"
    "catch(e){}}"
    "(async()=>{await st();try{const j=await(await fetch('/r')).json();"
    "L=(j.r||[]).map(r=>({n:r.n||'',p:r.p|0,cs:parse(r.t||'')}));"
    "O(j.d|0,'def');draw()}catch(e){}})();"
    "setInterval(st,2000);"
    "</script>";

static const char TOTP_PAGE[] =
    "<!doctype html><html lang=zh-CN><meta charset=utf-8>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<title>TOTP</title>"
    "<style>"
    "*{box-sizing:border-box}"
    "body{margin:0;background:#f4f5f7;color:#202124;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI','Microsoft YaHei',sans-serif}"
    "main{max-width:520px;margin:16px auto;padding:22px 18px;background:#fff;"
    "border-radius:20px;box-shadow:0 8px 28px rgba(30,35,40,.1)}"
    "h1{margin:0 0 8px;font-size:20px;font-weight:700}"
    "p{margin:0 0 10px;color:#70757a;font-size:14px}"
    ".c{background:#f8f9fa;padding:12px;margin:0 0 8px;border-radius:12px;border:1px solid #e3e5e8}"
    "input,select{width:100%;height:48px;padding:0 13px;margin:4px 0;font-size:15px;"
    "background:#fff;color:#202124;border:1px solid #d9dce1;"
    "border-radius:12px;box-sizing:border-box;outline:none}"
    "input:focus,select:focus{border-color:#cf3043;box-shadow:0 0 0 3px rgba(207,48,67,.1)}"
    "select:disabled,input:disabled{opacity:.4}"
    ".r{display:flex;gap:8px}.r>*{flex:1;width:auto}"
    "button{width:100%;margin-top:8px;height:50px;font-size:16px;"
    "font-weight:700;background:#c81d31;color:#fff;border:0;border-radius:13px;"
    "box-shadow:0 6px 16px rgba(200,29,49,.25)}"
    ".c button{width:auto;height:auto;padding:6px 12px;font-size:14px;margin:4px 0 0;background:#f3f4f6;color:#202124;box-shadow:none}"
    "#ok{min-height:1.4em;color:#c81d31}"
    "</style>"
    "<main><h1 id=h>TOTP</h1><p id=st></p><div id=list></div>"
    "<button type=button id=add>+</button>"
    "<button type=button id=sv>Save</button><p id=ok></p></main>"
    "<script>"
    "let T={ok:'Sent',fail:'Failed',app:'',nm:'',sec:'',max:32},L=[];"
    "const E=s=>String(s||'').replace(/[&<>\"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;'}[c]));"
    "function uri(s){const o={};s=String(s||'');const i=s.indexOf('?');"
    "if(!/^otpauth:\\/\\//i.test(s)||i<0)return o;"
    "s.slice(i+1).split('&').forEach(p=>{const j=p.indexOf('=');"
    "if(j>0)try{o[decodeURIComponent(p.slice(0,j)).toLowerCase()]=decodeURIComponent(p.slice(j+1))}catch(e){}});"
    "return o}"
    "function dp(r){const q=uri(r.s),d=+q.digits,p=+q.period,ld=d===6||d===8,lp=p>=15&&p<=120;"
    "return{d:ld?d:+(r.d||6),p:lp?p:+(r.p||30),ld,lp}}"
    "function draw(){"
    "document.getElementById('list').innerHTML=L.map((r,i)=>{"
    "const x=dp(r);"
    "return `<div class=c><input data-k=i maxlength=24 placeholder=\"${E(T.app)}\" value=\"${E(r.i)}\">`"
    "+`<input data-k=l maxlength=32 placeholder=\"${E(T.nm)}\" value=\"${E(r.l)}\">`"
    "+`<input data-k=s maxlength=512 placeholder=\"${E(T.sec)}\" value=\"${E(r.s)}\">`"
    "+`<div class=r><select data-k=d${x.ld?' disabled':''}><option value=6${x.d==8?'':' selected'}>6</option>`"
    "+`<option value=8${x.d==8?' selected':''}>8</option></select>`"
    "+`<input data-k=p type=number min=15 max=120${x.lp?' disabled':''} value=\"${x.p}\"></div>`"
    "+`<button type=button data-d=${i}>x</button></div>`}).join('')}"
    "function read(){L=[...document.querySelectorAll('.c')].map(c=>("
    "{i:c.querySelector('[data-k=i]').value,l:c.querySelector('[data-k=l]').value,"
    "s:c.querySelector('[data-k=s]').value,d:+c.querySelector('[data-k=d]').value,"
    "p:+c.querySelector('[data-k=p]').value}))}"
    "document.getElementById('list').onclick=e=>{const d=e.target.getAttribute('data-d');"
    "if(d==null)return;read();L.splice(+d,1);draw()};"
    "document.getElementById('list').oninput=e=>{"
    "if(e.target.getAttribute('data-k')!='s')return;"
    "const c=e.target.closest('.c');if(!c)return;"
    "const x=dp({s:e.target.value,d:c.querySelector('[data-k=d]').value,"
    "p:c.querySelector('[data-k=p]').value});"
    "const d=c.querySelector('[data-k=d]'),p=c.querySelector('[data-k=p]');"
    "d.disabled=x.ld;if(x.ld)d.value=x.d;p.disabled=x.lp;if(x.lp)p.value=x.p};"
    "document.getElementById('add').onclick=()=>{read();if(L.length>=T.max)return;"
    "L.push({i:'',l:'',s:'',d:6,p:30});draw()};"
    "document.getElementById('sv').onclick=async()=>{read();"
    "try{const r=await fetch('/o',{method:'POST',"
    "headers:{'Content-Type':'application/json'},"
    "body:JSON.stringify({a:L})});"
    "document.getElementById('ok').textContent=r.ok?T.ok:T.fail}"
    "catch(e){document.getElementById('ok').textContent=T.fail}};"
    "async function st(){try{const j=await(await fetch('/s')).json();"
    "document.documentElement.lang=j.lang||'en';"
    "if(j.mode!='totp')return;"
    "T.ok=j.ok;T.fail=j.fail;T.app=j.app;T.nm=j.nm;T.sec=j.sec;T.max=j.max||32;"
    "document.getElementById('h').textContent=j.title;"
    "document.getElementById('add').textContent=j.add;"
    "document.getElementById('sv').textContent=j.send;"
    "document.getElementById('st').textContent=j.syn||''}"
    "catch(e){}}"
    "(async()=>{await st();try{const j=await(await fetch('/o')).json();L=j.a||[];"
    "draw()}catch(e){}})();"
    "setInterval(st,2000);"
    "</script>";

#define APP_WEB_RULES_MAX 1536
#define APP_WEB_TOTP_MAX  6144

static httpd_handle_t s_httpd;
static SemaphoreHandle_t s_mu;
static uint32_t s_retry_at;
static bool s_ble_suspended;
static bool s_net_owned;
static bool s_ota_suspended;
static volatile bool s_ap_want;
static TaskHandle_t s_ap_task;
static TickType_t s_ap_try_at;
static lv_timer_t *s_ble_tm;
static lv_timer_t *s_ap_begin_tm;
static int s_ble_try;
static bool s_qr_shown;
static uint8_t s_cfg_step;
static void server_start(void);
static void server_stop(void);
static void ble_restore_kick(void);
static void cfg_ble_enter(void);
static void captive_attach(void);
static void captive_detach(void);

/* 设置蓝牙页/配对中保栈。广播期间也不准 HTTP 卸栈。 */
static bool ble_protect_adv(void)
{
    if (s_qr_shown && s_cfg_step == 0 && s_ap_want) return false;
    if (s_cfg_step == 1 || s_ota_suspended) return true;
    if (bsp_ble_adv_active()) return true;
    bsp_ble_state_t st = bsp_ble_state();
    return st == BSP_BLE_PAIRING || st == BSP_BLE_WAIT_NOTIFY ||
           st == BSP_BLE_CONNECTED || st == BSP_BLE_ANCS ||
           bsp_ble_conn_count() > 0;
}

static void wifi_need(void)
{
    if (bsp_wifi_enabled() || bsp_wifi_ap_active()) bsp_wifi_radio_resume();
}

static void ap_task(void *arg)
{
    (void)arg;
    if (s_ap_want) {
        if (bsp_wifi_init() == ESP_OK) bsp_wifi_ap_start();
        if (s_ap_want) {
            wifi_need();
            s_retry_at = 0;
            server_start();
            if (!s_httpd && s_ap_want && bsp_ble_stack_up() &&
                bsp_ble_conn_count() == 0 && !ble_protect_adv()) {
                bsp_ble_suspend();
                s_ble_suspended = true;
                s_retry_at = 0;
                server_start();
            }
            if (s_httpd) captive_attach();
        } else if (bsp_wifi_ap_active()) {
            captive_detach();
            bsp_wifi_ap_stop();
        }
    }
    s_ap_task = NULL;
    vTaskDelete(NULL);
}

static void ap_kick(bool now)
{
    s_ap_want = true;
    if (s_ap_task) return;
    if (bsp_wifi_ap_active() && s_httpd) {
        captive_attach();
        return;
    }
    TickType_t t = xTaskGetTickCount();
    if (!now && s_ap_try_at && t - s_ap_try_at < pdMS_TO_TICKS(1000)) return;
    s_ap_try_at = t;
    if (xTaskCreate(ap_task, "web_ap", 4096, NULL, 4, &s_ap_task) != pdPASS) {
        s_ap_task = NULL;
        ESP_LOGW(TAG, "web_ap task fail heap=%u largest=%u",
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    }
}

static void ap_begin_cb(lv_timer_t *t)
{
    (void)t;
    s_ap_begin_tm = NULL;
    if (!s_qr_shown || s_cfg_step || !s_ap_want) return;
    if (bsp_ble_stack_up()) {
        ESP_LOGI(TAG, "配置页停 BLE,腾 RAM");
        bsp_ble_suspend();
        s_ble_suspended = true;
        s_ap_begin_tm = lv_timer_create(ap_begin_cb, 300, NULL);
        lv_timer_set_repeat_count(s_ap_begin_tm, 1);
        return;
    }
    ap_kick(true);
}

static void ap_begin(void)
{
    s_ap_want = true;
    if (s_ap_task || (bsp_wifi_ap_active() && s_httpd)) return;
    if (s_ap_begin_tm) return;
    s_ap_begin_tm = lv_timer_create(ap_begin_cb, 50, NULL);
    lv_timer_set_repeat_count(s_ap_begin_tm, 1);
}

static void mu_ensure(void)
{
    if (!s_mu) s_mu = xSemaphoreCreateMutex();
}

static char s_pending[APP_WEB_TEXT_MAX + 1];
static bool s_have;
static bool s_fresh;

static char s_field[16];
static char *s_buf;
static size_t s_cap;
static void (*s_refresh)(void);

static bool s_rules;
static void (*s_rules_refresh)(void);
static app_kw_t s_rules_kw[APP_KW_MAX];
static uint8_t s_rules_n;
static uint8_t s_rules_def;
static bool s_rules_have;

static bool s_totp;
static void (*s_totp_refresh)(void);
static app_totp_acct_t *s_totp_items;
static uint16_t s_totp_n;
static bool s_totp_have;

static char s_shown_text[APP_WEB_TEXT_MAX + 1];
static bool s_shown;

static lv_obj_t *s_screen;
static lv_obj_t *s_qr_box, *s_qr_draw, *s_qr_url, *s_qr_hint;
static QRCode s_qr;
static uint8_t s_qr_mod[128];
static char s_qr_text[36];
static bool s_qr_ok;
static lv_obj_t *s_note, *s_note_lab;
static lv_timer_t *s_note_tm;
static uint8_t s_save_kind;

bool app_web_url(char *buf, size_t n)
{
    if (!buf || n < 32) return false;
    buf[0] = 0;
    if (bsp_wifi_ap_active()) {
        char ip[20];
        if (bsp_wifi_ap_ip(ip, sizeof(ip)) != ESP_OK || !ip[0]) {
            strcpy(ip, "192.168.4.1");
        }
        if (APP_WEB_HTTP_PORT == 80) snprintf(buf, n, "http://%s/", ip);
        else snprintf(buf, n, "http://%s:%d/", ip, APP_WEB_HTTP_PORT);
        return true;
    }
    if (bsp_wifi_state() != BSP_WIFI_CONNECTED) return false;
    char ip[20];
    if (bsp_wifi_ip(ip, sizeof(ip)) != ESP_OK) return false;
    if (!ip[0] || strcmp(ip, "0.0.0.0") == 0) return false;
    if (APP_WEB_HTTP_PORT == 80) snprintf(buf, n, "http://%s/", ip);
    else snprintf(buf, n, "http://%s:%d/", ip, APP_WEB_HTTP_PORT);
    return true;
}

static int hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void url_decode(char *s)
{
    char *d = s;
    for (; *s; s++) {
        if (*s == '+') {
            *d++ = ' ';
            continue;
        }
        if (*s == '%' && s[1] && s[2]) {
            int h = hex(s[1]), l = hex(s[2]);
            if (h >= 0 && l >= 0) {
                *d++ = (char)((h << 4) | l);
                s += 2;
                continue;
            }
        }
        *d++ = *s;
    }
    *d = 0;
}

static void trim_ws(char *s)
{
    char *e = s + strlen(s);
    while (e > s && (e[-1] == '\r' || e[-1] == '\n' || e[-1] == ' ')) *--e = 0;
    char *p = s;
    while (*p == ' ' || *p == '\r' || *p == '\n') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
}

static void store_text(const char *src)
{
    if (!src) return;
    char tmp[APP_WEB_TEXT_MAX * 3 + 8];
    ui_pixel_utf8_copy(tmp, sizeof(tmp), src);
    trim_ws(tmp);
    if (!tmp[0]) return;
    xSemaphoreTake(s_mu, portMAX_DELAY);
    ui_pixel_utf8_copy(s_pending, sizeof(s_pending), tmp);
    s_have = true;
    s_fresh = true;
    xSemaphoreGive(s_mu);
}

static void skip_ws(const char **p)
{
    while (**p == ' ' || **p == '\n' || **p == '\r' || **p == '\t') (*p)++;
}

static bool json_str(const char **p, char *out, size_t n)
{
    skip_ws(p);
    if (**p != '"') return false;
    (*p)++;
    size_t o = 0;
    while (**p && **p != '"') {
        char c = *(*p)++;
        if (c == '\\' && **p) c = *(*p)++;
        if (o + 1 < n) out[o++] = c;
    }
    if (**p != '"') return false;
    (*p)++;
    out[o] = 0;
    return true;
}

static bool json_int(const char **p, int *out)
{
    skip_ws(p);
    int sign = 1;
    if (**p == '-') {
        sign = -1;
        (*p)++;
    }
    if (**p < '0' || **p > '9') return false;
    int v = 0;
    while (**p >= '0' && **p <= '9') v = v * 10 + (*(*p)++ - '0');
    *out = v * sign;
    return true;
}

static bool skip_val(const char **p)
{
    skip_ws(p);
    if (**p == '"') {
        char dump[8];
        return json_str(p, dump, sizeof(dump));
    }
    if (**p == '-' || (**p >= '0' && **p <= '9')) {
        int v;
        return json_int(p, &v);
    }
    return false;
}

static uint8_t clamp_alert(int v)
{
    if (v < APP_ALERT_SILENT) return APP_ALERT_SILENT;
    if (v > APP_ALERT_DROP) return APP_ALERT_DROP;
    return (uint8_t)v;
}

static bool parse_rules(const char *s, app_kw_t *out, int *n, uint8_t *def)
{
    if (!s || !out || !n || !def) return false;
    *n = 0;
    *def = APP_ALERT_POPUP;
    const char *p = strchr(s, '{');
    if (!p) return false;
    p++;
    while (*p && *p != '}') {
        skip_ws(&p);
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p == '}') break;
        char key[8];
        if (!json_str(&p, key, sizeof(key))) return false;
        skip_ws(&p);
        if (*p != ':') return false;
        p++;
        if (!strcmp(key, "d")) {
            int v = 0;
            if (!json_int(&p, &v)) return false;
            *def = clamp_alert(v);
        } else if (!strcmp(key, "r")) {
            skip_ws(&p);
            if (*p != '[') return false;
            p++;
            while (*p && *p != ']') {
                skip_ws(&p);
                if (*p == ',') {
                    p++;
                    continue;
                }
                if (*p != '{') return false;
                p++;
                app_kw_t kw;
                memset(&kw, 0, sizeof(kw));
                kw.prio = APP_ALERT_POPUP;
                while (*p && *p != '}') {
                    skip_ws(&p);
                    if (*p == ',') {
                        p++;
                        continue;
                    }
                    char k2[8];
                    if (!json_str(&p, k2, sizeof(k2))) return false;
                    skip_ws(&p);
                    if (*p != ':') return false;
                    p++;
                    if (!strcmp(k2, "n")) {
                        char tmp[APP_KW_NAME_LEN * 3 + 8];
                        if (!json_str(&p, tmp, sizeof(tmp))) return false;
                        ui_pixel_utf8_copy(kw.name, sizeof(kw.name), tmp);
                    } else if (!strcmp(k2, "t")) {
                        char tmp[APP_KW_LEN * 3 + 8];
                        if (!json_str(&p, tmp, sizeof(tmp))) return false;
                        ui_pixel_utf8_copy(kw.text, sizeof(kw.text), tmp);
                    } else if (!strcmp(k2, "p")) {
                        int v = 0;
                        if (!json_int(&p, &v)) return false;
                        kw.prio = clamp_alert(v);
                    } else if (!skip_val(&p)) {
                        return false;
                    }
                }
                if (*p != '}') return false;
                p++;
                if (kw.text[0] && *n < APP_KW_MAX) out[(*n)++] = kw;
            }
            if (*p != ']') return false;
            p++;
        } else if (!skip_val(&p)) {
            return false;
        }
    }
    return true;
}

static bool parse_totp(const char *s, app_totp_acct_t *out, int *n)
{
    if (!s || !out || !n) return false;
    *n = 0;
    const char *p = strchr(s, '{');
    if (!p) return false;
    p++;
    while (*p && *p != '}') {
        skip_ws(&p);
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p == '}') break;
        char key[8];
        if (!json_str(&p, key, sizeof(key))) return false;
        skip_ws(&p);
        if (*p != ':') return false;
        p++;
        if (!strcmp(key, "a")) {
            skip_ws(&p);
            if (*p != '[') return false;
            p++;
            while (*p && *p != ']') {
                skip_ws(&p);
                if (*p == ',') {
                    p++;
                    continue;
                }
                if (*p != '{') return false;
                p++;
                app_totp_acct_t a;
                memset(&a, 0, sizeof(a));
                char secret[APP_WEB_TEXT_MAX + 1];
                char iss[APP_TOTP_ISSUER_LEN * 3 + 8];
                char lab[APP_TOTP_LABEL_LEN * 3 + 8];
                secret[0] = iss[0] = lab[0] = 0;
                int digits = 0, period = 0;
                while (*p && *p != '}') {
                    skip_ws(&p);
                    if (*p == ',') {
                        p++;
                        continue;
                    }
                    char k2[8];
                    if (!json_str(&p, k2, sizeof(k2))) return false;
                    skip_ws(&p);
                    if (*p != ':') return false;
                    p++;
                    if (!strcmp(k2, "i")) {
                        if (!json_str(&p, iss, sizeof(iss))) return false;
                    } else if (!strcmp(k2, "l")) {
                        if (!json_str(&p, lab, sizeof(lab))) return false;
                    } else if (!strcmp(k2, "s")) {
                        if (!json_str(&p, secret, sizeof(secret))) return false;
                    } else if (!strcmp(k2, "d")) {
                        if (!json_int(&p, &digits)) return false;
                    } else if (!strcmp(k2, "p")) {
                        if (!json_int(&p, &period)) return false;
                    } else if (!skip_val(&p)) {
                        return false;
                    }
                }
                if (*p != '}') return false;
                p++;
                if (!secret[0]) continue;
                bool fill = !iss[0] && !lab[0];
                if (!app_totp_ingest(secret, &a, fill)) return false;
                if (iss[0]) ui_pixel_utf8_copy(a.issuer, sizeof(a.issuer), iss);
                if (lab[0]) ui_pixel_utf8_copy(a.label, sizeof(a.label), lab);
                if (!a.digits && digits) a.digits = (uint8_t)digits;
                if (!a.period && period) a.period = (uint8_t)period;
                if (!a.issuer[0] && !a.label[0]) {
                    strncpy(a.issuer, "TOTP", sizeof(a.issuer) - 1);
                }
                if (*n < APP_TOTP_WEB_MAX) out[(*n)++] = a;
            }
            if (*p != ']') return false;
            p++;
        } else if (!skip_val(&p)) {
            return false;
        }
    }
    return true;
}

static void json_esc(char *dst, size_t n, const char *src)
{
    size_t o = 0;
    if (!dst || n == 0) return;
    for (; src && *src && o + 1 < n; src++) {
        unsigned char c = (unsigned char)*src;
        if (c == '"' || c == '\\') {
            if (o + 2 >= n) break;
            dst[o++] = '\\';
            dst[o++] = (char)c;
        } else if (c >= 0x20) {
            dst[o++] = (char)c;
        }
    }
    dst[o] = 0;
}

static void hide(void)
{
    s_shown = false;
    s_shown_text[0] = 0;
}

static void save_note(uint8_t kind)
{
    mu_ensure();
    xSemaphoreTake(s_mu, portMAX_DELAY);
    s_save_kind = kind;
    xSemaphoreGive(s_mu);
}

static void note_hide(lv_timer_t *t)
{
    (void)t;
    s_note_tm = NULL;
    if (s_note) lv_obj_add_flag(s_note, LV_OBJ_FLAG_HIDDEN);
}

static void note_show(const char *msg)
{
    if (!s_screen || !msg || !msg[0]) return;
    if (!s_note) {
        s_note = lv_obj_create(s_screen);
        ui_pixel_strip_theme(s_note);
        lv_obj_set_size(s_note, APP_VIEW_W - 16, 36);
        lv_obj_align(s_note, LV_ALIGN_BOTTOM_MID, 0, -(APP_SAFE_BOTTOM + 8));
        lv_obj_set_style_bg_opa(s_note, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(s_note, 4, 0);
        lv_obj_set_style_border_width(s_note, 1, 0);
        lv_obj_set_style_pad_hor(s_note, 10, 0);
        s_note_lab = lv_label_create(s_note);
        lv_obj_set_style_text_font(s_note_lab, ui_pixel_font_cjk(), 0);
        lv_obj_set_width(s_note_lab, APP_VIEW_W - 36);
        lv_label_set_long_mode(s_note_lab, LV_LABEL_LONG_CLIP);
        lv_obj_center(s_note_lab);
    }
    lv_obj_set_style_bg_color(s_note, lv_color_hex(ui_style_text()), 0);
    lv_obj_set_style_border_color(s_note, lv_color_hex(ui_style_text()), 0);
    lv_obj_set_style_text_color(s_note_lab, lv_color_hex(ui_style_on()), 0);
    lv_label_set_text(s_note_lab, msg);
    lv_obj_remove_flag(s_note, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_note);
    if (s_note_tm) lv_timer_delete(s_note_tm);
    s_note_tm = lv_timer_create(note_hide, 2500, NULL);
    lv_timer_set_repeat_count(s_note_tm, 1);
}

static const char *save_kind_label(uint8_t kind)
{
    switch (kind) {
    case 1: return app_str(APP_STR_RULE);
    case 2: return app_str(APP_STR_WIFI);
    case 3: return app_str(APP_STR_BLUETOOTH);
    case 4: return app_str(APP_STR_DATETIME);
    case 5: return app_str(APP_STR_HOME_CODES);
    default: return NULL;
    }
}

static void flash_saved(uint8_t kind)
{
    const char *lab = save_kind_label(kind);
    if (!lab) return;
    char msg[48];
    snprintf(msg, sizeof(msg), "%s · %s", lab, app_str(APP_STR_WEB_SAVED));
    app_shell_wake();
    app_tone_play((int)app_prefs()->tone_msg);
    note_show(msg);
}

static void qr_paint_at(lv_layer_t *layer, const lv_area_t *coords)
{
    if (!s_qr_ok || !layer || !coords) return;

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_hex(0x000000);
    dsc.bg_opa = LV_OPA_COVER;
    dsc.border_width = 0;
    dsc.radius = 0;
    dsc.outline_width = 0;
    dsc.shadow_width = 0;

    int n = s_qr.size;
    int quiet = 1;
    int inner = n + quiet * 2;
    int w = (int)lv_area_get_width(coords);
    int h = (int)lv_area_get_height(coords);
    int scale = (w < h ? w : h) / inner;
    if (scale < 1) return;
    int ox = coords->x1 + (w - inner * scale) / 2;
    int oy = coords->y1 + (h - inner * scale) / 2;

    for (int y = 0; y < n; y++) {
        int x = 0;
        while (x < n) {
            if (!qrcode_getModule(&s_qr, (uint8_t)x, (uint8_t)y)) {
                x++;
                continue;
            }
            int x0 = x;
            while (x < n && qrcode_getModule(&s_qr, (uint8_t)x, (uint8_t)y)) x++;
            lv_area_t a;
            a.x1 = ox + (x0 + quiet) * scale;
            a.y1 = oy + (y + quiet) * scale;
            a.x2 = ox + (x + quiet) * scale - 1;
            a.y2 = a.y1 + scale - 1;
            lv_draw_rect(layer, &dsc, &a);
        }
    }
}

static void qr_draw_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) return;
    lv_obj_t *obj = lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    qr_paint_at(layer, &coords);
}

bool app_web_qr_prepare(void)
{
    char url[36];
    if (!app_web_url(url, sizeof(url))) {
        s_qr_ok = false;
        s_qr_text[0] = 0;
        return false;
    }
    server_start();
    if (s_qr_ok && strcmp(s_qr_text, url) == 0) return true;
    strlcpy(s_qr_text, url, sizeof(s_qr_text));
    memset(s_qr_mod, 0, sizeof(s_qr_mod));
    s_qr_ok = qrcode_initText(&s_qr, s_qr_mod, 3, ECC_MEDIUM, url) >= 0;
    return s_qr_ok;
}

void app_web_qr_paint(lv_layer_t *layer, const lv_area_t *box)
{
    qr_paint_at(layer, box);
}

static void mini_qr_draw(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) return;
    lv_obj_t *obj = lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t box;
    lv_obj_get_coords(obj, &box);
    qr_paint_at(layer, &box);
}

void app_web_mini_qr_bind(lv_obj_t *parent, lv_obj_t **qr, lv_obj_t **url)
{
    if (!parent || !qr || !url) return;
    if (*qr) return;
    *qr = lv_obj_create(parent);
    ui_pixel_strip_theme(*qr);
    lv_obj_set_size(*qr, APP_WEB_MINI_QR, APP_WEB_MINI_QR);
    lv_obj_set_style_bg_opa(*qr, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(*qr, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(*qr, 0, 0);
    lv_obj_set_style_radius(*qr, UI_RADIUS_SM, 0);
    lv_obj_add_event_cb(*qr, mini_qr_draw, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_flag(*qr, LV_OBJ_FLAG_HIDDEN);
    *url = lv_label_create(parent);
    lv_obj_set_style_text_font(*url, ui_pixel_font_14(), 0);
    lv_obj_set_style_text_color(*url, lv_color_hex(UI_MUTE), 0);
    lv_obj_set_style_text_align(*url, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(*url, APP_CONTENT_W);
    lv_label_set_long_mode(*url, LV_LABEL_LONG_CLIP);
    lv_obj_add_flag(*url, LV_OBJ_FLAG_HIDDEN);
}

void app_web_mini_qr_show(lv_obj_t *qr, lv_obj_t *url, bool on)
{
    if (!on || !app_web_qr_prepare()) {
        if (qr) lv_obj_add_flag(qr, LV_OBJ_FLAG_HIDDEN);
        if (url) lv_obj_add_flag(url, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (!qr || !url) return;
    lv_obj_align(url, LV_ALIGN_BOTTOM_MID, 0, -(APP_WEB_MINI_QR + APP_WEB_MINI_GAP + 2));
    lv_obj_align(qr, LV_ALIGN_BOTTOM_MID, 0, -2);
    char buf[36];
    if (app_web_url(buf, sizeof(buf))) lv_label_set_text(url, buf);
    lv_obj_remove_flag(qr, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(url, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(qr);
}

static void qr_refresh(void)
{
    if (!s_qr_url) return;
    char url[36];
    if (!bsp_wifi_ap_active() || !s_httpd || !app_web_url(url, sizeof(url))) {
        s_qr_ok = false;
        s_qr_text[0] = 0;
        if (s_qr_hint) lv_label_set_text(s_qr_hint, app_str(APP_STR_AP_STARTING));
        lv_label_set_text(s_qr_url, "");
        if (s_qr_draw) {
            lv_obj_add_flag(s_qr_draw, LV_OBJ_FLAG_HIDDEN);
            lv_obj_invalidate(s_qr_draw);
        }
        return;
    }
    if (s_qr_hint) {
        const char *ap = bsp_wifi_ap_ssid();
        lv_label_set_text_fmt(s_qr_hint, app_str(APP_STR_AP_HINT),
                              ap && ap[0] ? ap : "Passport");
    }
    lv_label_set_text(s_qr_url, url);
    if (s_qr_ok && strcmp(s_qr_text, url) == 0) {
        if (s_qr_draw) {
            lv_obj_remove_flag(s_qr_draw, LV_OBJ_FLAG_HIDDEN);
            lv_obj_invalidate(s_qr_draw);
        }
        return;
    }
    if (s_qr_draw) lv_obj_remove_flag(s_qr_draw, LV_OBJ_FLAG_HIDDEN);
    strlcpy(s_qr_text, url, sizeof(s_qr_text));
    memset(s_qr_mod, 0, sizeof(s_qr_mod));
    s_qr_ok = qrcode_initText(&s_qr, s_qr_mod, 3, ECC_MEDIUM, url) >= 0;
    if (s_qr_draw) lv_obj_invalidate(s_qr_draw);
}

static void qr_ui_ensure(void)
{
    if (s_qr_box || !s_screen) return;
    s_qr_box = lv_obj_create(s_screen);
    ui_pixel_strip_theme(s_qr_box);
    lv_obj_set_pos(s_qr_box, APP_VIEW_X, APP_VIEW_Y);
    lv_obj_set_size(s_qr_box, APP_VIEW_W, APP_VIEW_H);
    ui_pixel_card_style(s_qr_box, UI_CARD, 0);
    lv_obj_set_style_pad_all(s_qr_box, 8, 0);

    int inner_w = APP_VIEW_W - 4 - 8;
    int inner_h = APP_VIEW_H - 4 - 8;
    int url_h = 18;
    int hint_h = 32;
    int gap = 6;
    int sz = inner_w;
    int rest = inner_h - url_h - hint_h - gap * 2;
    if (sz > rest) sz = rest;

    s_qr_hint = lv_label_create(s_qr_box);
    lv_obj_set_style_text_font(s_qr_hint, ui_pixel_font_cjk(), 0);
    lv_obj_set_style_text_color(s_qr_hint, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_style_text_align(s_qr_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_qr_hint, inner_w);
    lv_label_set_long_mode(s_qr_hint, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_qr_hint, app_str(APP_STR_AP_STARTING));
    lv_obj_align(s_qr_hint, LV_ALIGN_TOP_MID, 0, 0);

    s_qr_url = lv_label_create(s_qr_box);
    lv_obj_set_style_text_font(s_qr_url, ui_pixel_font_14(), 0);
    lv_obj_set_style_text_color(s_qr_url, lv_color_hex(UI_CYAN), 0);
    lv_obj_set_style_text_align(s_qr_url, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_qr_url, inner_w);
    lv_label_set_long_mode(s_qr_url, LV_LABEL_LONG_CLIP);
    lv_label_set_text(s_qr_url, "");
    lv_obj_align(s_qr_url, LV_ALIGN_TOP_MID, 0, hint_h + 2);

    s_qr_draw = lv_obj_create(s_qr_box);
    ui_pixel_strip_theme(s_qr_draw);
    lv_obj_set_size(s_qr_draw, sz, sz);
    lv_obj_set_style_bg_opa(s_qr_draw, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_qr_draw, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(s_qr_draw, 1, 0);
    lv_obj_set_style_border_color(s_qr_draw, lv_color_hex(UI_VIOLET), 0);
    lv_obj_align(s_qr_draw, LV_ALIGN_TOP_MID, 0, hint_h + url_h + gap);
    lv_obj_add_flag(s_qr_draw, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_qr_draw, qr_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
}

static bool cfg_http(void)
{
    return s_qr_shown && s_cfg_step == 0;
}

static bool ble_linked(void)
{
    bsp_ble_state_t st = bsp_ble_state();
    return bsp_ble_conn_count() > 0 ||
           st == BSP_BLE_CONNECTED || st == BSP_BLE_ANCS ||
           st == BSP_BLE_WAIT_NOTIFY;
}

static app_str_id_t ble_st_str(bsp_ble_state_t st)
{
    switch (st) {
    case BSP_BLE_ADVERTISING: return APP_STR_ST_ADV;
    case BSP_BLE_PAIRING:     return APP_STR_ST_PAIR;
    case BSP_BLE_WAIT_NOTIFY: return APP_STR_ST_WAIT;
    case BSP_BLE_CONNECTED:   return APP_STR_ST_CONN;
    case BSP_BLE_ANCS:        return APP_STR_ST_ANCS;
    default:                  return APP_STR_ST_IDLE;
    }
}

static void ble_refresh(void)
{
    if (s_cfg_step == 1 && bsp_ble_enabled() && bsp_ble_stack_up() &&
        !bsp_ble_adv_active())
        bsp_ble_set_advertising(true);
    if (!s_qr_hint) return;
    if (s_qr_draw) lv_obj_add_flag(s_qr_draw, LV_OBJ_FLAG_HIDDEN);
    const char *name = bsp_ble_name();
    if (!name || !name[0]) name = "Passport";
    bool up = bsp_ble_stack_up() && bsp_ble_synced() &&
               (bsp_ble_adv_active() || ble_linked());
    if (!up) {
        lv_label_set_text(s_qr_hint, app_str(APP_STR_BLE_STARTING));
        if (s_qr_url) lv_label_set_text(s_qr_url, app_str(APP_STR_ST_IDLE));
        return;
    }
    if (ble_linked()) lv_label_set_text(s_qr_hint, app_str(APP_STR_BLE_DONE));
    else {
        char setup[96];
        snprintf(setup, sizeof(setup), app_str(APP_STR_BLE_SETUP), name);
        lv_label_set_text_fmt(s_qr_hint, "%s\n%s", setup, app_str(APP_STR_BLE_FORGET));
    }
    if (!s_qr_url) return;
    bsp_ble_state_t st = bsp_ble_state();
    if (st == BSP_BLE_PAIRING && bsp_ble_passkey()) {
        lv_label_set_text_fmt(s_qr_url, "%s %06lu",
                             app_str(APP_STR_ST_PAIR),
                             (unsigned long)bsp_ble_passkey());
    } else {
        lv_label_set_text(s_qr_url, app_str(ble_st_str(st)));
    }
    lv_obj_remove_flag(s_qr_url, LV_OBJ_FLAG_HIDDEN);
}

static void http_stop(void)
{
    s_ap_want = false;
    if (s_ap_begin_tm) {
        lv_timer_delete(s_ap_begin_tm);
        s_ap_begin_tm = NULL;
    }
    bool hold = false;
    if (s_mu) {
        xSemaphoreTake(s_mu, portMAX_DELAY);
        hold = (s_buf && s_cap) || s_rules || s_totp;
        xSemaphoreGive(s_mu);
    }
    if (bsp_wifi_ap_active()) {
        captive_detach();
        bsp_wifi_ap_stop();
    }
    if (!hold) server_stop();
    if (bsp_wifi_enabled()) bsp_wifi_radio_resume();
    else bsp_wifi_radio_suspend();
}

static void qr_show(void)
{
    qr_ui_ensure();
    if (!s_qr_box) return;
    s_qr_shown = true;
    lv_obj_move_foreground(s_qr_box);
    lv_obj_remove_flag(s_qr_box, LV_OBJ_FLAG_HIDDEN);
    s_retry_at = 0;
    if (s_ble_tm) {
        lv_timer_delete(s_ble_tm);
        s_ble_tm = NULL;
    }
    s_ble_try = 0;
}

void app_web_qr_open(void)
{
    qr_show();
    if (!s_qr_box) return;
    s_cfg_step = 0;
    s_ota_suspended = false;
    qr_refresh();
    if (bsp_ble_stack_up()) {
        ESP_LOGI(TAG, "配置页停 BLE,腾 RAM");
        bsp_ble_suspend();
        s_ble_suspended = true;
    }
    ap_begin();
    app_shell_wake();
}

static void ble_restore(void)
{
    /* 对齐设置页 ble_kick_adv:先停 HTTP,再等 NimBLE 同步,最后才广播。 */
    if (s_cfg_step == 1) {
        app_web_suspend_for_ota(true);
        if (!bsp_ble_enabled()) return;
        s_ble_suspended = false;
        if (!bsp_ble_stack_up() || !bsp_ble_synced()) {
            bsp_ble_resume();
            return;
        }
        bsp_ble_set_advertising(true);
        return;
    }
    if (s_ap_want || cfg_http() || s_ap_task) return;
    if (!bsp_ble_enabled()) return;
    if (!bsp_ble_stack_up() || !bsp_ble_synced()) {
        bsp_ble_resume();
        return;
    }
    if (bsp_ble_conn_count() > 0) {
        if (bsp_ble_adv_active()) bsp_ble_set_advertising(false);
        return;
    }
    if (!bsp_ble_adv_active()) bsp_ble_set_advertising(true);
}

static void ble_restore_cb(lv_timer_t *t)
{
    (void)t;
    ble_restore();
    bool done = cfg_http() || s_ap_want || !bsp_ble_enabled();
    if (s_ap_task) done = false;
    if (!done && bsp_ble_stack_up() && bsp_ble_synced()) {
        if (bsp_ble_adv_active() || bsp_ble_conn_count() > 0) done = true;
    }
    if (!done && s_ble_try >= 25) done = true;
    if (done) {
        s_ble_tm = NULL;
        s_ble_try = 0;
        return;
    }
    s_ble_try++;
    s_ble_tm = lv_timer_create(ble_restore_cb, 400, NULL);
    lv_timer_set_repeat_count(s_ble_tm, 1);
}

static void ble_restore_kick_in(uint32_t ms)
{
    s_ble_try = 0;
    if (s_ble_tm) {
        lv_timer_delete(s_ble_tm);
        s_ble_tm = NULL;
    }
    s_ble_tm = lv_timer_create(ble_restore_cb, ms, NULL);
    lv_timer_set_repeat_count(s_ble_tm, 1);
}

static void ble_restore_kick(void)
{
    ble_restore_kick_in(50);
}

static void cfg_ble_enter(void)
{
    s_cfg_step = 1;
    s_ap_want = false;
    if (s_ap_begin_tm) {
        lv_timer_delete(s_ap_begin_tm);
        s_ap_begin_tm = NULL;
    }
    if (bsp_wifi_ap_active()) bsp_wifi_ap_stop();
    bsp_wifi_radio_suspend();
    if (s_qr_draw) lv_obj_add_flag(s_qr_draw, LV_OBJ_FLAG_HIDDEN);
    if (s_qr_hint) lv_obj_align(s_qr_hint, LV_ALIGN_TOP_MID, 0, 8);
    if (s_qr_url) lv_obj_align(s_qr_url, LV_ALIGN_BOTTOM_MID, 0, 0);
    s_ble_suspended = false;
    ble_refresh();
    ble_restore_kick_in(400);
    app_shell_wake();
}

void app_web_qr_close(void)
{
    s_qr_shown = false;
    s_cfg_step = 0;
    if (s_qr_box) lv_obj_delete(s_qr_box);
    s_qr_box = s_qr_draw = s_qr_url = s_qr_hint = NULL;
    http_stop();
    app_web_suspend_for_ota(false);
    ble_restore_kick();
}

bool app_web_qr_visible(void)
{
    return s_qr_shown;
}

bool app_web_keep_awake(void)
{
    return s_qr_shown || bsp_wifi_ap_active();
}

bool app_web_httpd_up(void)
{
    return s_httpd != NULL;
}

bool app_web_qr_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (!s_qr_shown) return false;
    if (s_cfg_step && btn == BSP_BTN_DOWN &&
        (ev == BSP_BTN_CLICK || ev == BSP_BTN_LONG)) {
        if (bsp_ble_stack_up()) bsp_ble_forget_all();
        else bsp_ble_resume();
        ble_refresh();
        return true;
    }
    if (btn == BSP_BTN_OK && (ev == BSP_BTN_CLICK || ev == BSP_BTN_LONG)) {
        if (s_cfg_step == 0) cfg_ble_enter();
        else if (ble_linked()) app_web_qr_close();
        else if (ev == BSP_BTN_LONG) app_web_qr_open();
        return true;
    }
    return true;
}

static esp_err_t send_html(httpd_req_t *req)
{
    ESP_LOGI(TAG, "GET /");
    xSemaphoreTake(s_mu, portMAX_DELAY);
    bool totp = s_totp;
    bool rules = s_rules;
    bool text = s_buf && s_cap;
    xSemaphoreGive(s_mu);
    const char *page = totp ? TOTP_PAGE : (text && !rules ? PAGE : RULES_PAGE);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t send_status(httpd_req_t *req)
{
    char json[1280];
    xSemaphoreTake(s_mu, portMAX_DELAY);
    bool totp = s_totp;
    if (totp) {
        snprintf(json, sizeof(json),
                 "{\"lang\":\"%s\",\"mode\":\"totp\",\"title\":\"%s\","
                 "\"add\":\"%s\",\"send\":\"%s\",\"ok\":\"%s\",\"fail\":\"%s\","
                 "\"app\":\"%s\",\"nm\":\"%s\",\"sec\":\"%s\",\"syn\":\"%s\",\"max\":%d}",
                 app_lang_html(),
                 app_str(APP_STR_HOME_CODES),
                 app_str(APP_STR_TOTP_ADD),
                 app_str(APP_STR_SAVE),
                 app_str(APP_STR_WEB_SENT),
                 app_str(APP_STR_WEB_FAIL),
                 app_str(APP_STR_TOTP_APP),
                 app_str(APP_STR_TOTP_NAME),
                 app_str(APP_STR_TOTP_HINT_SEC),
                 app_str(APP_STR_TOTP_HINT_SEC),
                 APP_TOTP_WEB_MAX);
    } else if (s_buf && s_cap) {
        const char *f = s_field[0] ? s_field : "";
        snprintf(json, sizeof(json),
                 "{\"lang\":\"%s\",\"field\":\"%s\",\"idle\":\"%s\","
                 "\"busy\":\"%s\",\"send\":\"%s\",\"ph\":\"%s\","
                 "\"ok\":\"%s\",\"fail\":\"%s\",\"hint\":\"%s\"}",
                 app_lang_html(), f,
                 app_str(APP_STR_WEB_NO_PAGE),
                 app_str(APP_STR_WEB_BUSY),
                 app_str(APP_STR_WEB_SEND),
                 app_str(APP_STR_WEB_PLACEHOLDER),
                 app_str(APP_STR_WEB_SENT),
                 app_str(APP_STR_WEB_FAIL),
                 app_str(APP_STR_WEB_HINT));
    } else {
        snprintf(json, sizeof(json),
                 "{\"lang\":\"%s\",\"mode\":\"rules\",\"title\":\"%s\","
                 "\"add\":\"%s\",\"send\":\"%s\",\"ok\":\"%s\",\"fail\":\"%s\","
                 "\"def\":\"%s\",\"nm\":\"%s\","
                 "\"s0\":\"%s\",\"s1\":\"%s\",\"s2\":\"%s\",\"s3\":\"%s\",\"max\":%d,"
                 "\"ac\":\"%s\","
                 "\"f\":[\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"],"
                 "\"o\":[\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"],"
                 "\"j\":[\"%s\",\"%s\",\"%s\",\"%s\"]}",
                 app_lang_html(),
                 app_str(APP_STR_RULE),
                 app_str(APP_STR_RULE_NEW),
                 app_str(APP_STR_SAVE),
                 app_str(APP_STR_WEB_SENT),
                 app_str(APP_STR_WEB_FAIL),
                 app_str(APP_STR_RULE_DEFAULT),
                 app_str(APP_STR_RULE_NAME),
                 app_str(APP_STR_ALERT_SILENT),
                 app_str(APP_STR_ALERT_POPUP),
                 app_str(APP_STR_ALERT_URGENT),
                 app_str(APP_STR_ALERT_DROP),
                 APP_KW_MAX,
                 app_str(APP_STR_RULE_ADD),
                 app_str(APP_STR_RULE_F_ANY),
                 app_str(APP_STR_RULE_F_TITLE),
                 app_str(APP_STR_RULE_F_SUB),
                 app_str(APP_STR_RULE_F_MSG),
                 app_str(APP_STR_RULE_F_APP),
                 app_str(APP_STR_RULE_F_NAME),
                 app_str(APP_STR_RULE_F_CAT),
                 app_str(APP_STR_RULE_OP_HAS),
                 app_str(APP_STR_RULE_OP_NOT),
                 app_str(APP_STR_RULE_OP_HEAD),
                 app_str(APP_STR_RULE_OP_TAIL),
                 app_str(APP_STR_RULE_OP_MATCH),
                 app_str(APP_STR_RULE_OP_EMPTY),
                 app_str(APP_STR_RULE_J_AND),
                 app_str(APP_STR_RULE_J_OR),
                 app_str(APP_STR_RULE_J_ANDNOT),
                 app_str(APP_STR_RULE_J_ORNOT));
    }
    xSemaphoreGive(s_mu);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t recv_text(httpd_req_t *req)
{
    int total = req->content_len;
    if (total < 0 || total > 512) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "too long");
        return ESP_FAIL;
    }
    char raw[513];
    int got = 0;
    while (got < total) {
        int n = httpd_req_recv(req, raw + got, (size_t)(total - got));
        if (n <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv");
            return ESP_FAIL;
        }
        got += n;
    }
    raw[got] = 0;

    char ctype[64] = { 0 };
    httpd_req_get_hdr_value_str(req, "Content-Type", ctype, sizeof(ctype));
    char *text = raw;
    if (strstr(ctype, "application/x-www-form-urlencoded")) {
        url_decode(raw);
        if (!strncmp(raw, "t=", 2)) text = raw + 2;
        char *amp = strchr(text, '&');
        if (amp) *amp = 0;
    }
    store_text(text);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(req, "ok");
}

static esp_err_t send_rules(httpd_req_t *req)
{
    char *json = malloc(2048);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_FAIL;
    }
    const app_prefs_t *p = app_prefs();
    int n = snprintf(json, 2048, "{\"d\":%u,\"r\":[", (unsigned)p->notif_def);
    size_t o = n > 0 ? (size_t)n : 0;
    char ns[APP_KW_NAME_LEN * 2 + 8];
    char ts[APP_KW_LEN * 2 + 8];
    for (int i = 0; i < p->kw_n && o + 8 < 2048; i++) {
        json_esc(ns, sizeof(ns), p->kw[i].name);
        json_esc(ts, sizeof(ts), p->kw[i].text);
        n = snprintf(json + o, 2048 - o, "%s{\"n\":\"%s\",\"t\":\"%s\",\"p\":%u}",
                     i ? "," : "", ns, ts, (unsigned)p->kw[i].prio);
        if (n < 0 || (size_t)n >= 2048 - o) break;
        o += (size_t)n;
    }
    if (o + 3 < 2048) {
        memcpy(json + o, "]}", 3);
        o += 2;
    }
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t e = httpd_resp_send(req, json, (ssize_t)o);
    free(json);
    return e;
}

static esp_err_t recv_rules(httpd_req_t *req)
{
    int total = req->content_len;
    if (total < 0 || total > APP_WEB_RULES_MAX) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "too long");
        return ESP_FAIL;
    }
    char *raw = malloc((size_t)total + 1);
    if (!raw) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_FAIL;
    }
    int got = 0;
    while (got < total) {
        int n = httpd_req_recv(req, raw + got, (size_t)(total - got));
        if (n <= 0) {
            free(raw);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv");
            return ESP_FAIL;
        }
        got += n;
    }
    raw[got] = 0;
    app_kw_t *tmp = malloc(sizeof(app_kw_t) * APP_KW_MAX);
    int n = 0;
    uint8_t def = APP_ALERT_POPUP;
    bool ok = tmp && parse_rules(raw, tmp, &n, &def);
    if (!ok) {
        free(tmp);
        free(raw);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad");
        return ESP_FAIL;
    }
    xSemaphoreTake(s_mu, portMAX_DELAY);
    memset(s_rules_kw, 0, sizeof(s_rules_kw));
    if (n > 0) memcpy(s_rules_kw, tmp, sizeof(app_kw_t) * (size_t)n);
    s_rules_n = (uint8_t)n;
    s_rules_def = def;
    s_rules_have = true;
    xSemaphoreGive(s_mu);
    free(tmp);
    free(raw);
    save_note(1);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(req, "ok");
}

static esp_err_t send_totp(httpd_req_t *req)
{
    xSemaphoreTake(s_mu, portMAX_DELAY);
    bool totp = s_totp;
    xSemaphoreGive(s_mu);
    if (!totp) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no");
        return ESP_FAIL;
    }
    app_totp_list_t *l = app_totp_store();
    int n = l ? (int)l->n : 0;
    if (n > APP_TOTP_WEB_MAX) n = APP_TOTP_WEB_MAX;
    size_t cap = 64 + (size_t)n * 280 + 16;
    if (cap < 128) cap = 128;
    char *json = malloc(cap);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_FAIL;
    }
    int w = snprintf(json, cap, "{\"a\":[");
    size_t o = w > 0 ? (size_t)w : 0;
    char is[APP_TOTP_ISSUER_LEN * 2 + 8];
    char ls[APP_TOTP_LABEL_LEN * 2 + 8];
    char ss[APP_TOTP_SECRET_LEN * 2 + 8];
    for (int i = 0; i < n && l && l->items && o + 8 < cap; i++) {
        json_esc(is, sizeof(is), l->items[i].issuer);
        json_esc(ls, sizeof(ls), l->items[i].label);
        json_esc(ss, sizeof(ss), l->items[i].secret);
        w = snprintf(json + o, cap - o,
                     "%s{\"i\":\"%s\",\"l\":\"%s\",\"s\":\"%s\",\"d\":%u,\"p\":%u}",
                     i ? "," : "", is, ls, ss,
                     (unsigned)(l->items[i].digits ? l->items[i].digits : 6),
                     (unsigned)(l->items[i].period ? l->items[i].period : 30));
        if (w < 0 || (size_t)w >= cap - o) break;
        o += (size_t)w;
    }
    if (o + 3 < cap) {
        memcpy(json + o, "]}", 3);
        o += 2;
    }
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t e = httpd_resp_send(req, json, (ssize_t)o);
    free(json);
    return e;
}

static esp_err_t recv_totp(httpd_req_t *req)
{
    xSemaphoreTake(s_mu, portMAX_DELAY);
    bool totp = s_totp;
    xSemaphoreGive(s_mu);
    if (!totp) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no");
        return ESP_FAIL;
    }
    int total = req->content_len;
    if (total < 0 || total > APP_WEB_TOTP_MAX) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "too long");
        return ESP_FAIL;
    }
    char *raw = malloc((size_t)total + 1);
    if (!raw) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_FAIL;
    }
    int got = 0;
    while (got < total) {
        int n = httpd_req_recv(req, raw + got, (size_t)(total - got));
        if (n <= 0) {
            free(raw);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv");
            return ESP_FAIL;
        }
        got += n;
    }
    raw[got] = 0;
    app_totp_acct_t *tmp = malloc(sizeof(app_totp_acct_t) * APP_TOTP_WEB_MAX);
    int n = 0;
    bool ok = tmp && parse_totp(raw, tmp, &n);
    if (!ok) {
        free(tmp);
        free(raw);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad");
        return ESP_FAIL;
    }
    xSemaphoreTake(s_mu, portMAX_DELAY);
    free(s_totp_items);
    if (n > 0) {
        s_totp_items = tmp;
        tmp = NULL;
    } else {
        s_totp_items = NULL;
    }
    s_totp_n = (uint16_t)n;
    s_totp_have = true;
    xSemaphoreGive(s_mu);
    free(tmp);
    free(raw);
    save_note(5);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(req, "ok");
}

static bool json_get_str(const char *s, const char *key, char *out, size_t n)
{
    char pat[12];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(s, pat);
    if (!p) return false;
    p = strchr(p + strlen(pat), ':');
    if (!p) return false;
    p++;
    return json_str(&p, out, n);
}

static bool json_get_int(const char *s, const char *key, int *out)
{
    char pat[12];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(s, pat);
    if (!p) return false;
    p = strchr(p + strlen(pat), ':');
    if (!p) return false;
    p++;
    return json_int(&p, out);
}

static bool recv_json(httpd_req_t *req, char *buf, size_t n)
{
    int total = req->content_len;
    if (total < 0 || total > 400 || (size_t)total + 1 > n) return false;
    int got = 0;
    while (got < total) {
        int r = httpd_req_recv(req, buf + got, (size_t)(total - got));
        if (r <= 0) return false;
        got += r;
    }
    buf[got] = 0;
    return true;
}

static esp_err_t send_wifi_cfg(httpd_req_t *req)
{
    wifi_need();
    if (!bsp_wifi_enabled()) bsp_wifi_set_enabled(true);
    bsp_wifi_ap_t aps[BSP_WIFI_SCAN_MAX];
    int n = bsp_wifi_scan(aps, BSP_WIFI_SCAN_MAX);
    if (n < 0) n = 0;
    char *json = malloc(1400);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_FAIL;
    }
    char ip[20] = { 0 };
    char ss[BSP_WIFI_SSID_MAX * 2 + 8];
    bsp_wifi_ip(ip, sizeof(ip));
    json_esc(ss, sizeof(ss), bsp_wifi_ssid());
    int w = snprintf(json, 1400, "{\"ssid\":\"%s\",\"st\":%d,\"ip\":\"%s\",\"n\":[",
                     ss, (int)bsp_wifi_state(), ip);
    size_t o = w > 0 ? (size_t)w : 0;
    for (int i = 0; i < n && o + 48 < 1400; i++) {
        char ns[BSP_WIFI_SSID_MAX * 2 + 8];
        json_esc(ns, sizeof(ns), aps[i].ssid);
        w = snprintf(json + o, 1400 - o, "%s{\"s\":\"%s\",\"r\":%d,\"o\":%u}",
                     i ? "," : "", ns, (int)aps[i].rssi, aps[i].open ? 1u : 0u);
        if (w < 0 || (size_t)w >= 1400 - o) break;
        o += (size_t)w;
    }
    if (o + 3 < 1400) {
        memcpy(json + o, "]}", 3);
        o += 2;
    }
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t e = httpd_resp_send(req, json, (ssize_t)o);
    free(json);
    return e;
}

static esp_err_t recv_wifi_cfg(httpd_req_t *req)
{
    char raw[160];
    if (!recv_json(req, raw, sizeof(raw))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad");
        return ESP_FAIL;
    }
    int forget = 0;
    if (json_get_int(raw, "forget", &forget) && forget) {
        bsp_wifi_forget();
        save_note(2);
        httpd_resp_set_type(req, "text/plain; charset=utf-8");
        return httpd_resp_sendstr(req, "ok");
    }
    char ssid[BSP_WIFI_SSID_MAX + 1] = { 0 };
    char pass[BSP_WIFI_PASS_MAX + 1] = { 0 };
    if (!json_get_str(raw, "s", ssid, sizeof(ssid)) || !ssid[0]) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ssid");
        return ESP_FAIL;
    }
    json_get_str(raw, "p", pass, sizeof(pass));
    wifi_need();
    if (!bsp_wifi_enabled()) bsp_wifi_set_enabled(true);
    if (bsp_wifi_connect(ssid, pass) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "wifi");
        return ESP_FAIL;
    }
    save_note(2);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(req, "ok");
}

static esp_err_t send_ble_cfg(httpd_req_t *req)
{
    bsp_ble_peer_t peers[BSP_BLE_PEER_MAX];
    int n = bsp_ble_list_peers(peers, BSP_BLE_PEER_MAX);
    char *json = malloc(900);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_FAIL;
    }
    char name[BSP_BLE_NAME_MAX * 2 + 8];
    json_esc(name, sizeof(name), bsp_ble_name());
    int w = snprintf(json, 900,
                     "{\"en\":%u,\"st\":%d,\"adv\":%u,\"name\":\"%s\",\"p\":[",
                     bsp_ble_enabled() ? 1u : 0u, (int)bsp_ble_state(),
                     bsp_ble_adv_active() ? 1u : 0u, name);
    size_t o = w > 0 ? (size_t)w : 0;
    for (int i = 0; i < n && o + 48 < 900; i++) {
        char pn[BSP_BLE_NAME_MAX * 2 + 8];
        char pa[40];
        json_esc(pn, sizeof(pn), peers[i].name);
        json_esc(pa, sizeof(pa), peers[i].addr);
        w = snprintf(json + o, 900 - o,
                     "%s{\"n\":\"%s\",\"a\":\"%s\",\"c\":%u,\"b\":%u}",
                     i ? "," : "", pn, pa,
                     peers[i].connected ? 1u : 0u, peers[i].bonded ? 1u : 0u);
        if (w < 0 || (size_t)w >= 900 - o) break;
        o += (size_t)w;
    }
    if (o + 3 < 900) {
        memcpy(json + o, "]}", 3);
        o += 2;
    }
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t e = httpd_resp_send(req, json, (ssize_t)o);
    free(json);
    return e;
}

static esp_err_t recv_ble_cfg(httpd_req_t *req)
{
    char raw[64];
    if (!recv_json(req, raw, sizeof(raw))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad");
        return ESP_FAIL;
    }
    int v = 0;
    if (json_get_int(raw, "forget", &v)) {
        bsp_ble_forget_at(v);
    } else if (json_get_int(raw, "en", &v)) {
        if (v && bsp_wifi_ap_active()) bsp_wifi_ap_stop();
        bsp_ble_set_enabled(v != 0);
    } else if (json_get_int(raw, "adv", &v)) {
        if (v && s_qr_shown && s_cfg_step == 0) {
            save_note(3);
            httpd_resp_set_type(req, "text/plain; charset=utf-8");
            esp_err_t e = httpd_resp_sendstr(req, "ok");
            cfg_ble_enter();
            return e;
        }
        bsp_ble_set_advertising(v != 0);
    }
    save_note(3);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(req, "ok");
}

static esp_err_t send_clock_cfg(httpd_req_t *req)
{
    int y = 0, mo = 0, d = 0, h = 0, mi = 0;
    app_time_get(&y, &mo, &d, &h, &mi);
    const app_prefs_t *p = app_prefs();
    char json[320];
    snprintf(json, sizeof(json),
             "{\"ntp\":%u,\"srv\":%u,\"sync\":%u,\"y\":%d,\"mo\":%d,\"d\":%d,"
             "\"h\":%d,\"mi\":%d,\"sv\":[\"%s\",\"%s\",\"%s\",\"%s\"]}",
             p->ntp_on ? 1u : 0u, (unsigned)p->ntp_server,
             app_time_ntp_synced() ? 1u : 0u, y, mo, d, h, mi,
             app_ntp_server(0), app_ntp_server(1),
             app_ntp_server(2), app_ntp_server(3));
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t recv_clock_cfg(httpd_req_t *req)
{
    char raw[160];
    if (!recv_json(req, raw, sizeof(raw))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad");
        return ESP_FAIL;
    }
    app_prefs_t *p = app_prefs();
    int v = 0;
    bool ntp_changed = false;
    if (json_get_int(raw, "ntp", &v)) {
        p->ntp_on = v ? 1 : 0;
        ntp_changed = true;
    }
    if (json_get_int(raw, "srv", &v) && v >= 0 && v < APP_NTP_SERVER_N) {
        p->ntp_server = (uint8_t)v;
        ntp_changed = true;
    }
    int y = 0, mo = 0, d = 0, h = 0, mi = 0;
    if (json_get_int(raw, "y", &y) && json_get_int(raw, "mo", &mo) &&
        json_get_int(raw, "d", &d) && json_get_int(raw, "h", &h) &&
        json_get_int(raw, "mi", &mi) && y >= 2020) {
        app_time_set(y, mo, d, h, mi);
    }
    if (ntp_changed) {
        app_prefs_save();
        app_time_ntp_restart();
    }
    save_note(4);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(req, "ok");
}

static volatile bool s_dns_on;
static TaskHandle_t s_dns_task;
static int s_dns_sock = -1;
static char s_captive_uri[36];
static bool s_dhcp_captive;

static uint32_t captive_ip(void)
{
    esp_netif_t *n = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    esp_netif_ip_info_t ip;
    if (n && esp_netif_get_ip_info(n, &ip) == ESP_OK && ip.ip.addr)
        return ip.ip.addr;
    return ESP_IP4TOADDR(192, 168, 4, 1);
}

static int dns_build(const uint8_t *req, int n, uint8_t *out, int max)
{
    if (n < 12 || n + 16 > max) return -1;
    memcpy(out, req, (size_t)n);
    out[2] |= 0x80;
    int i = 12;
    while (i < n) {
        uint8_t lab = out[i];
        if (lab == 0) {
            i++;
            break;
        }
        if (lab >= 0xC0) {
            i += 2;
            break;
        }
        i += lab + 1;
    }
    uint16_t typ = 0;
    if (i + 2 <= n) typ = ((uint16_t)out[i] << 8) | out[i + 1];
    out[6] = 0;
    out[7] = 0;
    if (typ != 1) return n;
    out[7] = 1;
    uint8_t *a = out + n;
    a[0] = 0xC0;
    a[1] = 0x0C;
    a[2] = 0;
    a[3] = 1;
    a[4] = 0;
    a[5] = 1;
    a[6] = 0;
    a[7] = 0;
    a[8] = 1;
    a[9] = 0x2C;
    a[10] = 0;
    a[11] = 4;
    uint32_t ip = captive_ip();
    memcpy(a + 12, &ip, 4);
    return n + 16;
}

static void dns_task(void *arg)
{
    (void)arg;
    uint8_t rx[256];
    uint8_t tx[272];
    while (s_dns_on) {
        int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (sock < 0) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        int yes = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        struct sockaddr_in addr = {
            .sin_family = AF_INET,
            .sin_port = htons(53),
            .sin_addr.s_addr = htonl(INADDR_ANY),
        };
        if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            close(sock);
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        s_dns_sock = sock;
        ESP_LOGI(TAG, "dns :53");
        while (s_dns_on) {
            struct sockaddr_in src;
            socklen_t sl = sizeof(src);
            int n = recvfrom(sock, rx, sizeof(rx), 0, (struct sockaddr *)&src, &sl);
            if (n < 0) break;
            int m = dns_build(rx, n, tx, (int)sizeof(tx));
            if (m > 0) sendto(sock, tx, m, 0, (struct sockaddr *)&src, sl);
        }
        s_dns_sock = -1;
        close(sock);
    }
    s_dns_task = NULL;
    vTaskDelete(NULL);
}

static void dns_stop(void)
{
    s_dns_on = false;
    int sock = s_dns_sock;
    if (sock >= 0) shutdown(sock, SHUT_RDWR);
}

static void dns_start(void)
{
    if (!bsp_wifi_ap_active() || s_dns_task) {
        if (s_dns_task) s_dns_on = true;
        return;
    }
    s_dns_on = true;
    if (xTaskCreate(dns_task, "web_dns", 2560, NULL, 5, &s_dns_task) != pdPASS) {
        s_dns_task = NULL;
        s_dns_on = false;
        ESP_LOGW(TAG, "dns task fail");
    }
}

static void captive_dhcp(void)
{
    if (s_dhcp_captive || !bsp_wifi_ap_active()) return;
    if (!app_web_url(s_captive_uri, sizeof(s_captive_uri))) {
        strcpy(s_captive_uri, "http://192.168.4.1/");
    }
    size_t n = strlen(s_captive_uri);
    if (n > 1 && s_captive_uri[n - 1] == '/') s_captive_uri[--n] = 0;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (!netif) return;
    esp_netif_dhcps_stop(netif);
    esp_err_t e = esp_netif_dhcps_option(netif, ESP_NETIF_OP_SET,
                                         ESP_NETIF_CAPTIVEPORTAL_URI,
                                         s_captive_uri, n);
    esp_netif_dhcps_start(netif);
    if (e == ESP_OK) s_dhcp_captive = true;
    else ESP_LOGW(TAG, "dhcp 114 %s", esp_err_to_name(e));
}

static void captive_attach(void)
{
    if (!bsp_wifi_ap_active() || !s_httpd) return;
    esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
    esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    esp_log_level_set("httpd_parse", ESP_LOG_ERROR);
    captive_dhcp();
    dns_start();
}

static void captive_detach(void)
{
    dns_stop();
    s_dhcp_captive = false;
}

static esp_err_t captive_404(httpd_req_t *req, httpd_err_code_t err)
{
    (void)err;
    httpd_resp_set_status(req, "200 OK");
    return send_html(req);
}

static const httpd_uri_t URI_ROOT = {
    .uri = "/", .method = HTTP_GET, .handler = send_html,
};
static const httpd_uri_t URI_STATUS = {
    .uri = "/s", .method = HTTP_GET, .handler = send_status,
};
static const httpd_uri_t URI_POST = {
    .uri = "/t", .method = HTTP_POST, .handler = recv_text,
};
static const httpd_uri_t URI_FORM = {
    .uri = "/", .method = HTTP_POST, .handler = recv_text,
};
static const httpd_uri_t URI_RULES_GET = {
    .uri = "/r", .method = HTTP_GET, .handler = send_rules,
};
static const httpd_uri_t URI_RULES_POST = {
    .uri = "/r", .method = HTTP_POST, .handler = recv_rules,
};
static const httpd_uri_t URI_TOTP_GET = {
    .uri = "/o", .method = HTTP_GET, .handler = send_totp,
};
static const httpd_uri_t URI_TOTP_POST = {
    .uri = "/o", .method = HTTP_POST, .handler = recv_totp,
};
static const httpd_uri_t URI_WIFI_GET = {
    .uri = "/w", .method = HTTP_GET, .handler = send_wifi_cfg,
};
static const httpd_uri_t URI_WIFI_POST = {
    .uri = "/w", .method = HTTP_POST, .handler = recv_wifi_cfg,
};
static const httpd_uri_t URI_BLE_GET = {
    .uri = "/b", .method = HTTP_GET, .handler = send_ble_cfg,
};
static const httpd_uri_t URI_BLE_POST = {
    .uri = "/b", .method = HTTP_POST, .handler = recv_ble_cfg,
};
static const httpd_uri_t URI_CLOCK_GET = {
    .uri = "/c", .method = HTTP_GET, .handler = send_clock_cfg,
};
static const httpd_uri_t URI_CLOCK_POST = {
    .uri = "/c", .method = HTTP_POST, .handler = recv_clock_cfg,
};

static void register_common(httpd_handle_t hd)
{
    httpd_register_uri_handler(hd, &URI_ROOT);
    httpd_register_uri_handler(hd, &URI_STATUS);
    httpd_register_uri_handler(hd, &URI_POST);
    httpd_register_uri_handler(hd, &URI_FORM);
    httpd_register_uri_handler(hd, &URI_RULES_GET);
    httpd_register_uri_handler(hd, &URI_RULES_POST);
    httpd_register_uri_handler(hd, &URI_TOTP_GET);
    httpd_register_uri_handler(hd, &URI_TOTP_POST);
    httpd_register_uri_handler(hd, &URI_WIFI_GET);
    httpd_register_uri_handler(hd, &URI_WIFI_POST);
    httpd_register_uri_handler(hd, &URI_BLE_GET);
    httpd_register_uri_handler(hd, &URI_BLE_POST);
    httpd_register_uri_handler(hd, &URI_CLOCK_GET);
    httpd_register_uri_handler(hd, &URI_CLOCK_POST);
    httpd_register_err_handler(hd, HTTPD_404_NOT_FOUND, captive_404);
}

static void server_start(void)
{
    if (s_httpd || s_ota_suspended || ble_protect_adv()) return;
    uint32_t now = xTaskGetTickCount();
    if (s_retry_at && now < s_retry_at) return;
    if (!app_net_acquire(APP_NET_WEB, 0)) {
        ESP_LOGW(TAG, "httpd skip net");
        s_retry_at = now + pdMS_TO_TICKS(2000);
        return;
    }
    s_net_owned = true;
    if (bsp_ble_stack_up() && bsp_ble_conn_count() == 0 && !ble_protect_adv()) {
        if (bsp_ble_suspend() != ESP_OK) {
            app_net_release(APP_NET_WEB);
            s_net_owned = false;
            return;
        }
        s_ble_suspended = true;
    }
    size_t blk = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!app_net_heap_ready(8 * 1024)) {
        ESP_LOGW(TAG, "httpd skip heap largest=%u", (unsigned)blk);
        s_retry_at = now + pdMS_TO_TICKS(8000);
        if (s_ble_suspended && !s_ap_want) {
            s_ble_suspended = false;
            bsp_ble_resume();
        }
        app_net_release(APP_NET_WEB);
        s_net_owned = false;
        return;
    }

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = APP_WEB_HTTP_PORT;
    cfg.ctrl_port = 32768;
    cfg.max_open_sockets = 2;
    cfg.lru_purge_enable = true;
    cfg.stack_size = 5120;
    cfg.max_uri_handlers = 18;
    esp_err_t e = httpd_start(&s_httpd, &cfg);
    if (e != ESP_OK) {
        /* 失败时 listen fd 可能没关,250ms 连着重试会 EADDRINUSE 把 socket 耗尽。 */
        s_httpd = NULL;
        s_retry_at = now + pdMS_TO_TICKS(8000);
        ESP_LOGE(TAG, "httpd start %s heap=%u largest=%u",
                 esp_err_to_name(e),
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        if (s_ble_suspended && !s_ap_want) {
            s_ble_suspended = false;
            bsp_ble_resume();
        }
        app_net_release(APP_NET_WEB);
        s_net_owned = false;
        return;
    }
    s_retry_at = 0;
    register_common(s_httpd);
    bsp_wifi_ps_hold();
    if (bsp_wifi_ap_active()) captive_attach();
    char ip[20] = { 0 };
    if (bsp_wifi_ap_active()) bsp_wifi_ap_ip(ip, sizeof(ip));
    else bsp_wifi_ip(ip, sizeof(ip));
    ESP_LOGI(TAG, "http://%s:%d/  heap=%u",
             ip[0] ? ip : "0.0.0.0", APP_WEB_HTTP_PORT,
             (unsigned)esp_get_free_heap_size());
}

static void server_stop(void)
{
    captive_detach();
    if (s_httpd) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
        bsp_wifi_ps_release();
    }
    if (s_ble_suspended) {
        s_ble_suspended = false;
        bsp_ble_resume();
    }
    if (s_net_owned) {
        app_net_release(APP_NET_WEB);
        s_net_owned = false;
    }
}

void app_web_suspend_for_ota(bool on)
{
    s_ota_suspended = on;
    if (on) server_stop();
    else if (cfg_http() || bsp_wifi_ap_active() ||
             (s_buf && s_cap) || s_rules || s_totp) {
        server_start();
    }
}

void app_web_init(lv_obj_t *screen)
{
    static const app_modal_t text_modal = {
        .visible = app_web_visible,
        .key = app_web_key,
        .prio = 70,
    };
    static const app_modal_t qr_modal = {
        .visible = app_web_qr_visible,
        .key = app_web_qr_key,
        .prio = 60,
    };
    app_shell_register_modal(&text_modal);
    app_shell_register_modal(&qr_modal);

    mu_ensure();
    s_screen = screen;
    hide();
}

void app_web_listen(void)
{
    mu_ensure();
}

void app_web_boot_setup(void)
{
    if (bsp_ble_paired()) {
        app_web_suspend_for_ota(true);
        ESP_LOGI(TAG, "配对停 WiFi rc=%s",
                 esp_err_to_name(bsp_wifi_radio_suspend()));
        s_ble_suspended = false;
        ble_restore_kick_in(50);
        return;
    }
    app_lock_hide();
    app_web_qr_open();
}

void app_web_poll(void)
{
    if (app_notif_pairing()) {
        if (s_shown) hide();
        if (s_qr_shown && s_cfg_step) ble_refresh();
        return;
    }
    if (s_qr_shown) {
        if (s_cfg_step == 0) {
            if (!ble_protect_adv() && (!bsp_wifi_ap_active() || !s_httpd)) ap_begin();
            qr_refresh();
        } else {
            ble_refresh();
        }
        if (s_qr_box) lv_obj_move_foreground(s_qr_box);
    }
    if (!s_mu) return;

    xSemaphoreTake(s_mu, portMAX_DELAY);
    uint8_t save_kind = s_save_kind;
    s_save_kind = 0;
    bool rules_have = s_rules_have;
    uint8_t rules_n = s_rules_n;
    uint8_t rules_def = s_rules_def;
    void (*rules_refresh)(void) = s_rules_refresh;
    app_kw_t *rules_copy = NULL;
    if (rules_have) {
        rules_copy = malloc(sizeof(s_rules_kw));
        if (rules_copy) memcpy(rules_copy, s_rules_kw, sizeof(s_rules_kw));
        s_rules_have = false;
    }
    bool totp_have = s_totp && s_totp_have;
    uint16_t totp_n = 0;
    app_totp_acct_t *totp_copy = NULL;
    void (*totp_refresh)(void) = s_totp_refresh;
    if (totp_have) {
        totp_n = s_totp_n;
        totp_copy = s_totp_items;
        s_totp_items = NULL;
        s_totp_n = 0;
        s_totp_have = false;
    }
    bool have = s_have && s_pending[0];
    bool target = s_buf && s_cap;
    bool fresh = s_fresh;
    char *buf = s_buf;
    size_t cap = s_cap;
    void (*refresh)(void) = s_refresh;
    char text[APP_WEB_TEXT_MAX + 1];
    text[0] = 0;
    if (have && target) {
        strlcpy(text, s_pending, sizeof(text));
        s_have = false;
        s_pending[0] = 0;
        s_fresh = false;
    }
    xSemaphoreGive(s_mu);

    if (rules_have) {
        if (rules_copy) {
            app_prefs_t *pr = app_prefs();
            memset(pr->kw, 0, sizeof(pr->kw));
            if (rules_n > 0) {
                memcpy(pr->kw, rules_copy, sizeof(app_kw_t) * (size_t)rules_n);
            }
            pr->kw_n = rules_n;
            pr->notif_def = rules_def;
            app_prefs_save();
            if (s_shown) hide();
            if (rules_refresh) rules_refresh();
            flash_saved(save_kind ? save_kind : 1);
        }
        free(rules_copy);
        free(totp_copy);
        return;
    }

    if (totp_have) {
        app_totp_list_t *l = app_totp_store();
        app_totp_list_clear(l);
        for (uint16_t i = 0; i < totp_n && totp_copy; i++) {
            app_totp_list_add(l, &totp_copy[i]);
        }
        app_totp_persist();
        free(totp_copy);
        if (s_shown) hide();
        if (totp_refresh) totp_refresh();
        flash_saved(save_kind ? save_kind : 5);
        return;
    }

    if (have && target) {
        if (s_shown) hide();
        ui_pixel_utf8_copy(buf, cap, text);
        if (refresh) refresh();
        app_shell_wake();
        if (fresh) app_tone_play((int)app_prefs()->tone_msg);
        return;
    }
    if (s_shown && !target) hide();
    if (save_kind) flash_saved(save_kind);
}

bool app_web_visible(void)
{
    return s_shown;
}

bool app_web_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (!s_shown) return false;
    if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {
        xSemaphoreTake(s_mu, portMAX_DELAY);
        s_have = false;
        s_pending[0] = 0;
        s_fresh = false;
        xSemaphoreGive(s_mu);
        hide();
        return true;
    }
    if (ev != BSP_BTN_CLICK) return true;
    if (btn != BSP_BTN_OK) return true;

    char text[APP_WEB_TEXT_MAX + 1];
    void (*refresh)(void) = NULL;
    xSemaphoreTake(s_mu, portMAX_DELAY);
    strlcpy(text, s_pending, sizeof(text));
    char *buf = s_buf;
    size_t cap = s_cap;
    refresh = s_refresh;
    s_have = false;
    s_pending[0] = 0;
    s_fresh = false;
    xSemaphoreGive(s_mu);

    hide();
    if (buf && cap) {
        ui_pixel_utf8_copy(buf, cap, text);
        if (refresh) refresh();
    }
    return true;
}

void app_web_set_target(const char *name, char *buf, size_t cap,
                        void (*refresh)(void))
{
    if (!s_mu) return;
    wifi_need();
    server_start();
    xSemaphoreTake(s_mu, portMAX_DELAY);
    s_buf = buf;
    s_cap = cap;
    s_refresh = refresh;
    s_field[0] = 0;
    if (name) {
        size_t i = 0;
        for (; name[i] && i + 1 < sizeof(s_field); i++) {
            char c = name[i];
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9')) {
                s_field[i] = c;
            } else {
                break;
            }
        }
        s_field[i] = 0;
    }
    xSemaphoreGive(s_mu);
}

void app_web_clear_target(void)
{
    if (!s_mu) return;
    xSemaphoreTake(s_mu, portMAX_DELAY);
    s_buf = NULL;
    s_cap = 0;
    s_refresh = NULL;
    s_field[0] = 0;
    bool hold = s_rules || s_totp;
    xSemaphoreGive(s_mu);
    if (!s_qr_shown && !hold && !bsp_wifi_ap_active()) server_stop();
}

void app_web_set_rules(void (*refresh)(void))
{
    if (!s_mu) return;
    wifi_need();
    server_start();
    xSemaphoreTake(s_mu, portMAX_DELAY);
    s_rules = true;
    s_rules_refresh = refresh;
    xSemaphoreGive(s_mu);
}

void app_web_clear_rules(void)
{
    if (!s_mu) return;
    xSemaphoreTake(s_mu, portMAX_DELAY);
    s_rules = false;
    s_rules_refresh = NULL;
    s_rules_have = false;
    s_rules_n = 0;
    bool hold = (s_buf && s_cap) || s_totp;
    xSemaphoreGive(s_mu);
    if (!s_qr_shown && !hold && !bsp_wifi_ap_active()) server_stop();
}

void app_web_set_totp(void (*refresh)(void))
{
    if (!s_mu) return;
    wifi_need();
    server_start();
    xSemaphoreTake(s_mu, portMAX_DELAY);
    s_totp = true;
    s_totp_refresh = refresh;
    xSemaphoreGive(s_mu);
}

void app_web_clear_totp(void)
{
    if (!s_mu) return;
    xSemaphoreTake(s_mu, portMAX_DELAY);
    s_totp = false;
    s_totp_refresh = NULL;
    s_totp_have = false;
    s_totp_n = 0;
    free(s_totp_items);
    s_totp_items = NULL;
    bool hold = (s_buf && s_cap) || s_rules;
    xSemaphoreGive(s_mu);
    if (!s_qr_shown && !hold && !bsp_wifi_ap_active()) server_stop();
}
