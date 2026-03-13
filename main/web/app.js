function showSection(sectionId){
var sections=document.querySelectorAll('.section');
var menuItems=document.querySelectorAll('.menu-item');
sections.forEach(function(s){s.classList.remove('active');});
menuItems.forEach(function(m){m.classList.remove('active');});
var el=document.getElementById(sectionId);if(el)el.classList.add('active');
var activeBtn=document.querySelector('[onclick*="'+sectionId+'"]'); if(activeBtn) activeBtn.classList.add('active');
}
function showAzureSection(){
const password=prompt('Azure IoT Configuration Access\\n\\nPlease enter the admin password to access Azure IoT Hub settings:');
if(password===null) return;
if(password==='admin123'){
showSection('azure');
}else{
alert('Access Denied\\n\\nIncorrect password. Azure IoT Hub configuration is protected for security reasons.\\n\\nContact your system administrator if you need access.');
}}
function updateSystemStatus(){
fetch('/api/system_status').then(response=>response.json()).then(data=>{
document.getElementById('uptime').textContent=data.system.uptime_formatted;
document.getElementById('mac_address').textContent=data.system.mac_address;
document.getElementById('flash_total').textContent=(data.system.flash_total/1024/1024).toFixed(1)+' MB';
document.getElementById('tasks').textContent=data.tasks.count;
document.getElementById('heap').textContent=(data.memory.free_heap/1024).toFixed(1)+' KB';
const heapUsage=document.getElementById('heap_usage');
heapUsage.textContent=data.memory.heap_usage_percent.toFixed(1)+'%';
heapUsage.className=data.memory.heap_usage_percent>80?'status-error':(data.memory.heap_usage_percent>60?'status-warning':'status-good');
document.getElementById('internal_heap').textContent=(data.memory.internal_heap/1024).toFixed(1)+' KB';
document.getElementById('spiram_heap').textContent=data.memory.spiram_heap>0?(data.memory.spiram_heap/1024).toFixed(1)+' KB':'Not Available';
document.getElementById('largest_block').textContent=(data.memory.largest_free_block/1024).toFixed(1)+' KB';
document.getElementById('app_partition').textContent=(data.partitions.app_partition_size/1024).toFixed(0)+' KB ('+data.partitions.app_usage_percent.toFixed(1)+'% used)';
document.getElementById('nvs_partition').textContent=(data.partitions.nvs_partition_size/1024).toFixed(0)+' KB ('+data.partitions.nvs_usage_percent.toFixed(1)+'% used)';
const wifiStatus=document.getElementById('wifi_status');
wifiStatus.textContent=data.wifi.status;
wifiStatus.className=data.wifi.status==='connected'?'status-good':'status-error';
const rssi=document.getElementById('rssi');
rssi.textContent=data.wifi.rssi+' dBm';
rssi.className=data.wifi.rssi>-50?'status-good':(data.wifi.rssi>-70?'status-warning':'status-error');
document.getElementById('ssid').textContent=data.wifi.ssid;
if(data.sim){
const simStatus=document.getElementById('sim_status');
if(simStatus){
simStatus.textContent=data.sim.status==='connected'?'Connected':'Disconnected';
simStatus.className=data.sim.status==='connected'?'status-good':'status-error';
}
const simSignal=document.getElementById('sim_signal');
if(simSignal){
if(data.sim.signal!==0){simSignal.textContent=data.sim.signal+' dBm ('+data.sim.signal_quality+')';}
else if(data.sim.status==='connected'){simSignal.textContent='Good';}
else{simSignal.textContent='N/A';}
}
const simNetwork=document.getElementById('sim_network');
if(simNetwork){
if(data.sim.operator&&data.sim.operator!=='Unknown'){simNetwork.textContent=data.sim.operator;}
else if(data.sim.status==='connected'){simNetwork.textContent='Connected';}
else{simNetwork.textContent='N/A';}
}
const simIp=document.getElementById('sim_ip');
if(simIp){
if(data.sim.ip&&data.sim.ip!=='N/A'){simIp.textContent=data.sim.ip;}
else if(data.sim.status==='connected'){simIp.textContent='Assigned';}
else{simIp.textContent='N/A';}
}
}
}).catch(err=>console.log('Status update failed:',err));
fetch('/api/modbus/status').then(r=>r.json()).then(data=>{
const ids=['modbus_','ov_modbus_'];
ids.forEach(prefix=>{
const el=document.getElementById(prefix+'total_reads');if(el)el.textContent=data.total_reads;
const el2=document.getElementById(prefix+'success');if(el2)el2.textContent=data.successful_reads;
const el3=document.getElementById(prefix+'failed');if(el3)el3.textContent=data.failed_reads;
const rate=document.getElementById(prefix+'success_rate');
if(rate){rate.textContent=data.success_rate.toFixed(1)+'%';rate.className=data.success_rate>95?'status-good':(data.success_rate>80?'status-warning':'status-error');}
const el5=document.getElementById(prefix+'crc_errors');if(el5)el5.textContent=data.crc_errors;
const el6=document.getElementById(prefix+'timeout_errors');if(el6)el6.textContent=data.timeout_errors;
});
}).catch(err=>console.log('Modbus status failed:',err));
fetch('/api/azure/status').then(r=>r.json()).then(data=>{
const ids=['azure_','ov_azure_'];
ids.forEach(prefix=>{
const conn=document.getElementById(prefix+'connection');
if(conn){conn.textContent=data.connection_state;conn.className=data.connection_state==='connected'?'status-good':'status-error';}
const hours=Math.floor(data.connection_uptime/3600);
const mins=Math.floor((data.connection_uptime%3600)/60);
const up=document.getElementById(prefix+'uptime');if(up)up.textContent=hours+'h '+mins+'m';
const msg=document.getElementById(prefix+'messages');if(msg)msg.textContent=data.messages_sent;
const lastTel=data.last_telemetry_ago;
const lt=document.getElementById(prefix+'last_telemetry');if(lt)lt.textContent=lastTel>0?lastTel+'s ago':'Never';
const rc=document.getElementById(prefix+'reconnects');if(rc)rc.textContent=data.reconnect_attempts;
const did=document.getElementById(prefix+'device_id');if(did)did.textContent=data.device_id;
});
}).catch(err=>console.log('Azure status failed:',err));}

function performWatchdogAction(action){
const btn=event.target;
const resultDiv=document.getElementById('watchdog-result');
if(action==='reset'){
if(!confirm('Are you sure you want to restart the system? This will disconnect all clients.'))return;
btn.disabled=true;
btn.textContent='🔄 Restarting...';
resultDiv.innerHTML='<span style="color:#ff9500">⚠️ System restart initiated. Device will reboot in 3 seconds...</span>';
resultDiv.style.display='block';
resultDiv.style.backgroundColor='#fff3cd';
resultDiv.style.borderColor='#ffeaa7';
resultDiv.style.color='#856404';
}
const formData=new URLSearchParams();formData.append('action',action);
fetch('/watchdog_control',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:formData})
.then(r=>r.json()).then(data=>{
if(data.status==='success'){
if(action==='reset'){
setTimeout(()=>{window.location.reload();},5000);
}else{
resultDiv.innerHTML='<span style="color:#28a745">✅ '+data.message+'</span>';
resultDiv.style.display='block';
resultDiv.style.backgroundColor='#d4edda';
resultDiv.style.borderColor='#c3e6cb';
resultDiv.style.color='#155724';
updateWatchdogStatus();
}
}else{
resultDiv.innerHTML='<span style="color:#dc3545">❌ Error: '+data.message+'</span>';
resultDiv.style.display='block';
resultDiv.style.backgroundColor='#f8d7da';
resultDiv.style.borderColor='#f5c6cb';
resultDiv.style.color='#721c24';
}
}).catch(e=>{
resultDiv.innerHTML='<span style="color:#dc3545">❌ Communication Error: '+e.message+'</span>';
resultDiv.style.display='block';
resultDiv.style.backgroundColor='#f8d7da';
resultDiv.style.borderColor='#f5c6cb';
resultDiv.style.color='#721c24';
}).finally(()=>{if(action!=='reset'){btn.disabled=false;btn.textContent=btn.textContent.replace('...','');}});
}

function updateWatchdogStatus(){
const formData=new URLSearchParams();formData.append('action','status');
fetch('/watchdog_control',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:formData})
.then(r=>r.json()).then(data=>{
if(data.status==='success'){
document.getElementById('wd-status').textContent=data.watchdog_enabled==='true'?'Enabled':'Disabled';
document.getElementById('wd-timeout').textContent=data.timeout_seconds+' seconds';
document.getElementById('wd-cpu0').textContent=data.cpu0_monitored==='true'?'Active':'Inactive';
document.getElementById('wd-cpu1').textContent=data.cpu1_monitored==='true'?'Active':'Inactive';
const uptimeMs=data.uptime_ms;
const hours=Math.floor(uptimeMs/3600000);
const minutes=Math.floor((uptimeMs%3600000)/60000);
const seconds=Math.floor((uptimeMs%60000)/1000);
document.getElementById('wd-uptime').textContent=hours+'h '+minutes+'m '+seconds+'s';
}
}).catch(err=>console.log('Watchdog status update failed:',err));
}

function setGPIO(){
const pin=document.getElementById('gpio-pin').value;
const value=document.getElementById('gpio-value').value;
const resultDiv=document.getElementById('gpio-result');
if(!pin||pin<0||pin>39){
resultDiv.innerHTML='<span style="color:#dc3545">❌ Invalid GPIO pin. Use 0-39.</span>';
resultDiv.style.display='block';
resultDiv.style.backgroundColor='#f8d7da';
return;
}
const formData=new URLSearchParams();
formData.append('action','set');
formData.append('pin',pin);
formData.append('value',value);
fetch('/gpio_trigger',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:formData})
.then(r=>r.json()).then(data=>{
if(data.status==='success'){
resultDiv.innerHTML='<span style="color:#28a745">✅ GPIO '+data.pin+' set to '+(data.value==1?'HIGH':'LOW')+'</span>';
resultDiv.style.backgroundColor='#d4edda';
}else{
resultDiv.innerHTML='<span style="color:#dc3545">❌ Error: '+data.message+'</span>';
resultDiv.style.backgroundColor='#f8d7da';
}
resultDiv.style.display='block';
}).catch(e=>{
resultDiv.innerHTML='<span style="color:#dc3545">❌ Communication Error: '+e.message+'</span>';
resultDiv.style.display='block';
resultDiv.style.backgroundColor='#f8d7da';
});
}

function readGPIO(){
const pin=document.getElementById('gpio-read-pin').value;
const resultDiv=document.getElementById('gpio-result');
if(!pin||pin<0||pin>39){
resultDiv.innerHTML='<span style="color:#dc3545">❌ Invalid GPIO pin. Use 0-39.</span>';
resultDiv.style.display='block';
resultDiv.style.backgroundColor='#f8d7da';
return;
}
const formData=new URLSearchParams();
formData.append('action','read');
formData.append('pin',pin);
fetch('/gpio_trigger',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:formData})
.then(r=>r.json()).then(data=>{
if(data.status==='success'){
resultDiv.innerHTML='<span style="color:#28a745">📖 GPIO '+data.pin+' is '+(data.value==1?'HIGH (1)':'LOW (0)')+'</span>';
resultDiv.style.backgroundColor='#d4edda';
}else{
resultDiv.innerHTML='<span style="color:#dc3545">❌ Error: '+data.message+'</span>';
resultDiv.style.backgroundColor='#f8d7da';
}
resultDiv.style.display='block';
}).catch(e=>{
resultDiv.innerHTML='<span style="color:#dc3545">❌ Communication Error: '+e.message+'</span>';
resultDiv.style.display='block';
resultDiv.style.backgroundColor='#f8d7da';
});
}

function quickGPIO(pin,value){
const formData=new URLSearchParams();
formData.append('action','set');
formData.append('pin',pin);
formData.append('value',value);
const resultDiv=document.getElementById('gpio-result');
fetch('/gpio_trigger',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:formData})
.then(r=>r.json()).then(data=>{
if(data.status==='success'){
resultDiv.innerHTML='<span style="color:#28a745">✅ Quick Control: GPIO '+pin+' set to '+(value==1?'HIGH':'LOW')+'</span>';
resultDiv.style.backgroundColor='#d4edda';
}else{
resultDiv.innerHTML='<span style="color:#dc3545">❌ Error: '+data.message+'</span>';
resultDiv.style.backgroundColor='#f8d7da';
}
resultDiv.style.display='block';
}).catch(e=>{
resultDiv.innerHTML='<span style="color:#dc3545">❌ Communication Error: '+e.message+'</span>';
resultDiv.style.display='block';
resultDiv.style.backgroundColor='#f8d7da';
});
}

function toggleNetworkMode(){
var w=document.getElementById('mode_wifi');var s=document.getElementById('mode_sim');
if(!w||!s)return;
var wm=w.checked;var sm=s.checked;
var e=document.getElementById('wifi_panel');if(e)e.style.display=wm?'block':'none';
e=document.getElementById('sim_panel');if(e)e.style.display=sm?'block':'none';
e=document.getElementById('wifi-network-status');if(e)e.style.display=wm?'block':'none';
e=document.getElementById('sim-network-status');if(e)e.style.display=sm?'block':'none';}
function toggleSDOptions(){
var c=document.getElementById('sd_enabled');if(!c)return;var en=c.checked;
var e=document.getElementById('sd_options');if(e)e.style.display=en?'block':'none';
e=document.getElementById('sd_hw_options');if(e)e.style.display=en?'block':'none';}
function toggleRTCOptions(){
var c=document.getElementById('rtc_enabled');if(!c)return;var en=c.checked;
var e=document.getElementById('rtc_options');if(e)e.style.display=en?'block':'none';
e=document.getElementById('rtc_hw_options');if(e)e.style.display=en?'block':'none';}
function toggleAzurePassword(){
var input=document.getElementById('azure_device_key');if(!input)return;
var toggle=event.target;
if(input.type==='password'){input.type='text';toggle.textContent='HIDE';}else{input.type='password';toggle.textContent='SHOW';}}
function refreshOtaStatus(){
fetch('/api/ota/status').then(function(r){return r.json();}).then(function(data){
var e=document.getElementById('ota_current_version');if(e)e.textContent=data.current_version||'Unknown';
e=document.getElementById('ota_current_partition');if(e)e.textContent=data.running_partition||'Unknown';
e=document.getElementById('ota_status');if(e)e.textContent=data.state||'Idle';
e=document.getElementById('ota_last_update');if(e&&data.last_update)e.textContent=data.last_update;
}).catch(function(e){console.error('OTA status error:',e);});}
function showSensorSubMenu(menuType){
var r=document.getElementById('regular-sensors-list');
var w=document.getElementById('water-quality-sensors-list');
var m=document.getElementById('modbus-explorer-list');
var br=document.getElementById('btn-regular-sensors');
var bw=document.getElementById('btn-water-quality-sensors');
var bm=document.getElementById('btn-modbus-explorer');
if(menuType==='regular'){if(r)r.style.display='block';if(w)w.style.display='none';if(m)m.style.display='none';if(br)br.style.background='#007bff';if(bw)bw.style.background='#6c757d';if(bm)bm.style.background='#6c757d';}
else if(menuType==='water_quality'){if(r)r.style.display='none';if(w)w.style.display='block';if(m)m.style.display='none';if(br)br.style.background='#6c757d';if(bw)bw.style.background='#17a2b8';if(bm)bm.style.background='#6c757d';}
else if(menuType==='explorer'){if(r)r.style.display='none';if(w)w.style.display='none';if(m)m.style.display='block';if(br)br.style.background='#6c757d';if(bw)bw.style.background='#6c757d';if(bm)bm.style.background='#6c757d';}}
window.onload=function(){const savedSection=sessionStorage.getItem('showSection');if(savedSection){sessionStorage.removeItem('showSection');if(savedSection==='azure'){showAzureSection();}else{showSection(savedSection);}}else{const hash=window.location.hash.substring(1);if(hash&&hash!==''){if(hash==='azure'){showAzureSection();}else{showSection(hash);}}else{showSection('overview');}}toggleNetworkMode();toggleSDOptions();toggleRTCOptions();updateSystemStatus();setInterval(updateSystemStatus,5000);refreshOtaStatus();if(document.getElementById('wd-uptime')){updateWatchdogStatus();setInterval(updateWatchdogStatus,30000);}}

