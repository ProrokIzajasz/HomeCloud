const $=selector=>document.querySelector(selector);
let savedSession=null;try{savedSession=JSON.parse(localStorage.getItem('homecloud-session')||'null')}catch{}if(savedSession&&savedSession.expiresAt<=Date.now()){localStorage.removeItem('homecloud-session');savedSession=null}
const state={token:savedSession?.token||'',user:savedSession?.user||'',path:'.',view:'files',previewEntries:[],previewIndex:0,previewCache:new Map(),previewRequests:new Map(),thumbnailCache:new Map(),thumbnailRequests:new Map(),naming:null};
const api=async(path,options={})=>{const headers=new Headers(options.headers||{});if(state.token)headers.set('Authorization',`Bearer ${state.token}`);const response=await fetch(path,{...options,headers});if(response.status===401){logout();throw new Error('Sesja wygasła. Zaloguj się ponownie.')}if(!response.ok){let message=`Błąd ${response.status}`;try{const body=await response.json();message=body.message||body.error||message}catch{}throw new Error(message)}return response};
const escapeHtml=value=>String(value).replace(/[&<>'"]/g,char=>({'&':'&amp;','<':'&lt;','>':'&gt;',"'":'&#39;','"':'&quot;'}[char]));
const encode=value=>encodeURIComponent(value);
const joinPath=(base,name)=>base==='.'?name:`${base}/${name}`;
const parentPath=path=>{if(path==='.')return'.';const parts=path.split('/');parts.pop();return parts.length?parts.join('/'):'.'};
const formatBytes=bytes=>{if(!bytes)return'0 B';const units=['B','KB','MB','GB','TB'];const index=Math.min(Math.floor(Math.log(bytes)/Math.log(1024)),4);return`${(bytes/1024**index).toFixed(index?1:0)} ${units[index]}`};
const toast=(message,error=false)=>{const node=$('#toast');node.textContent=message;node.style.background=error?'#8c352f':'#26321d';node.classList.remove('hidden');clearTimeout(toast.timer);toast.timer=setTimeout(()=>node.classList.add('hidden'),3200)};

function showApp(){
  $('#loginView').classList.add('hidden');$('#appView').classList.remove('hidden');
  $('#currentUser').textContent=state.user;$('#avatar').textContent=state.user.slice(0,1).toUpperCase();
  refreshAll();
}
function clearPreviewCache(){for(const cache of [state.previewCache,state.thumbnailCache]){for(const item of cache.values())URL.revokeObjectURL(item.url);cache.clear()}state.previewRequests.clear();state.thumbnailRequests.clear()}
function logout(){state.token='';state.user='';localStorage.removeItem('homecloud-session');clearPreviewCache();$('#appView').classList.add('hidden');$('#loginView').classList.remove('hidden');$('#password').value=''}

$('#loginForm').addEventListener('submit',async event=>{event.preventDefault();$('#loginError').textContent='';const username=$('#username').value.trim(),password=$('#password').value;try{const body=new URLSearchParams({username,password});const response=await fetch('/api/v1/login',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});if(!response.ok)throw new Error(response.status===429?'Za dużo prób. Odczekaj pięć minut.':'Nieprawidłowa nazwa lub hasło.');const result=await response.json();state.token=result.token;state.user=username;localStorage.setItem('homecloud-session',JSON.stringify({token:state.token,user:username,expiresAt:Date.now()+30*60*1000}));showApp()}catch(error){$('#loginError').textContent=error.message}});
$('#logoutButton').addEventListener('click',async()=>{try{await api('/api/v1/logout',{method:'POST'})}catch{}logout()});

async function loadStorage(){const response=await api('/api/v1/storage');const data=await response.json();const used=data.usedBytes/1024**3,quota=data.quotaBytes/1024**3;$('#storageText').textContent=`${used.toFixed(1)} GB z ${quota.toFixed(0)} GB`;$('#storageProgress').style.width=`${Math.min(100,data.usedBytes/data.quotaBytes*100)}%`}
async function loadFiles(){
  let endpoint,trash=false;
  if(state.view==='trash'){endpoint='/api/v1/trash';trash=true}else if(state.view==='search'){endpoint=`/api/v1/search?query=${encode($('#searchInput').value.trim())}`}else{endpoint=`/api/v1/files?path=${encode(state.path)}`}
  const response=await api(endpoint),data=await response.json();renderRows(data.entries||[],trash);
  $('#backButton').classList.toggle('hidden',state.path==='.'||state.view!=='files');
  $('#quickAccess').classList.toggle('hidden',state.path!=='.'||state.view!=='files');
  $('#pathLabel').textContent=state.view==='trash'?'Elementy można przywrócić':state.path==='.'?'HomeCloud':state.path;
  $('#sectionTitle').textContent=state.view==='trash'?'Kosz':state.view==='search'?'Wyniki wyszukiwania':'Szybki dostęp';
  $('#listTitle').textContent=state.view==='trash'?'Usunięte elementy':state.path==='.'?'Pliki':'Zawartość folderu';
}
const imageExtensions=['jpg','jpeg','png','gif','webp','bmp'];
const previewExtensions=[...imageExtensions,'pdf','mp4','webm','mov'];
const extension=path=>path.includes('.')?path.split('.').pop().toLowerCase():'';
function fileIcon(path){const ext=extension(path);if(imageExtensions.includes(ext))return'▧';if(ext==='pdf')return'PDF';if(['mp4','webm','mov'].includes(ext))return'▶';return'◇'}
async function cachedPreview(path){
  if(state.previewCache.has(path)){const item=state.previewCache.get(path);state.previewCache.delete(path);state.previewCache.set(path,item);return item}
  if(state.previewRequests.has(path))return state.previewRequests.get(path);
  const request=(async()=>{const response=await api(`/api/v1/preview?path=${encode(path)}`),blob=await response.blob(),item={blob,url:URL.createObjectURL(blob)};state.previewCache.set(path,item);while(state.previewCache.size>6){const [oldPath,oldItem]=state.previewCache.entries().next().value;state.previewCache.delete(oldPath);URL.revokeObjectURL(oldItem.url)}state.previewRequests.delete(path);return item})();
  state.previewRequests.set(path,request);return request;
}
async function cachedThumbnail(path){
  if(state.thumbnailCache.has(path))return state.thumbnailCache.get(path);
  if(state.thumbnailRequests.has(path))return state.thumbnailRequests.get(path);
  const request=(async()=>{const response=await api(`/api/v1/thumbnail?path=${encode(path)}`),blob=await response.blob(),item={url:URL.createObjectURL(blob)};state.thumbnailCache.set(path,item);while(state.thumbnailCache.size>40){const [oldPath,oldItem]=state.thumbnailCache.entries().next().value;state.thumbnailCache.delete(oldPath);URL.revokeObjectURL(oldItem.url)}state.thumbnailRequests.delete(path);return item})();
  state.thumbnailRequests.set(path,request);return request;
}
function renderRows(entries,trash){
  state.previewEntries=trash?[]:entries.filter(entry=>!entry.directory&&previewExtensions.includes(extension(entry.path)));
  const rows=$('#fileRows');rows.innerHTML='';$('#emptyState').classList.toggle('hidden',entries.length>0);
  for(const entry of entries){
    const path=trash?entry.originalPath:entry.path,directory=entry.directory??false,isImage=!directory&&imageExtensions.includes(extension(path));
    const row=document.createElement('tr');if(!trash)row.className='clickable';
    const icon=isImage?`<span class="file-icon photo-thumb"><img data-thumb-path="${escapeHtml(path)}" alt=""></span>`:`<span class="file-icon">${directory?'✉':fileIcon(path)}</span>`;
    row.innerHTML=`<td><div class="file-name">${icon}<span>${escapeHtml(path.split('/').pop())}</span></div></td><td>${trash?'Kosz':directory?'Folder':'Plik'}</td><td>${trash?'—':directory?'—':formatBytes(entry.sizeBytes)}</td><td><div class="row-actions">${trash?`<button data-action="restore" title="Przywróć">↶</button><button data-action="permanent" title="Usuń trwale">✕</button>`:directory?`<button data-action="open" title="Otwórz">›</button><button data-action="rename" title="Zmień nazwę">✎</button><button data-action="trash" title="Usuń">♲</button>`:`<button data-action="preview" title="Podgląd">◉</button><button data-action="rename" title="Zmień nazwę">✎</button><button data-action="download" title="Pobierz">↓</button><button data-action="trash" title="Usuń">♲</button>`}</div></td>`;
    row.addEventListener('click',event=>handleRow(entry,event.target.dataset.action||(!trash?(directory?'open':'preview'):''),trash));rows.appendChild(row);
  }
  loadVisibleThumbnails();
}
function loadVisibleThumbnails(){
  const load=image=>{if(image.dataset.queued)return;image.dataset.queued='1';state.thumbnailQueue=(state.thumbnailQueue||Promise.resolve()).then(()=>new Promise(resolve=>setTimeout(resolve,40))).then(async()=>{try{image.src=(await cachedThumbnail(image.dataset.thumbPath)).url}catch{if(image.isConnected)image.closest('.photo-thumb').textContent='▧'}})};
  const images=[...document.querySelectorAll('img[data-thumb-path]')];if(!('IntersectionObserver'in window)){images.forEach(load);return}
  const observer=new IntersectionObserver(items=>items.forEach(item=>{if(item.isIntersecting){observer.unobserve(item.target);load(item.target)}}),{rootMargin:'120px'});images.forEach(image=>observer.observe(image));
}
async function refreshAll(){try{await Promise.all([loadStorage(),loadFiles()])}catch(error){toast(error.message,true)}}

document.querySelectorAll('.nav-item[data-view]').forEach(button=>button.addEventListener('click',()=>{if(button.disabled)return;document.querySelectorAll('.nav-item').forEach(item=>item.classList.remove('active'));button.classList.add('active');state.view=button.dataset.view;state.path='.';loadFiles()}));
document.querySelectorAll('.folder-card').forEach(card=>card.addEventListener('click',async()=>{const folder=card.dataset.folder;try{await api(`/api/v1/directories?path=${encode(folder)}`,{method:'POST'});state.path=folder;state.view='files';await loadFiles()}catch(error){toast(error.message,true)}}));
$('#backButton').addEventListener('click',()=>{state.path=parentPath(state.path);loadFiles()});$('#refreshButton').addEventListener('click',refreshAll);
let searchTimer;$('#searchInput').addEventListener('input',()=>{clearTimeout(searchTimer);searchTimer=setTimeout(()=>{const value=$('#searchInput').value.trim();state.view=value?'search':'files';if(!value)state.path='.';loadFiles()},350)});

$('#addButton').addEventListener('click',event=>{const menu=$('#uploadMenu'),rect=event.currentTarget.getBoundingClientRect();menu.style.left=`${Math.max(10,rect.right-160)}px`;menu.style.top=`${rect.bottom+7}px`;menu.classList.toggle('hidden')});
$('#uploadMenu').addEventListener('click',event=>{const pick=event.target.dataset.pick;if(!pick)return;$('#uploadMenu').classList.add('hidden');const folder=pick==='folder';if(window.HomeCloudNative?.supportsBackgroundUploads?.()){window.HomeCloudNative.pickFiles(state.path,folder,state.token);return}$(folder?'#folderPicker':'#filePicker').click()});
$('#filePicker').addEventListener('change',event=>uploadFilesFast([...event.target.files],false));$('#folderPicker').addEventListener('change',event=>uploadFilesFast([...event.target.files],true));
async function uploadFiles(files,preserveTree){if(!files.length)return;let done=0;try{for(const file of files){const relative=preserveTree?joinPath(state.path,file.webkitRelativePath):joinPath(state.path,file.name);await api(`/api/v1/upload?relativePath=${encode(relative)}`,{method:'POST',headers:{'Content-Type':'application/octet-stream'},body:file});done++;toast(`Wysłano ${done} z ${files.length}`)}await refreshAll()}catch(error){toast(`Wysłano ${done} z ${files.length}. ${error.message}`,true)}finally{$('#filePicker').value='';$('#folderPicker').value=''}}

const uploadChunkSize=4*1024*1024;
const uploadWait=milliseconds=>new Promise(resolve=>setTimeout(resolve,milliseconds));
function showUploadProgress(done,total,completed,count){const panel=$('#uploadStatus');panel.classList.remove('hidden');$('#uploadDetails').textContent=`${completed}/${count} · ${formatBytes(done)} z ${formatBytes(total)}`;$('#uploadProgress').style.width=`${total?Math.min(100,done/total*100):100}%`}
async function withUploadRetry(job){let lastError;for(let attempt=0;attempt<4;attempt++){try{return await job()}catch(error){lastError=error;if(attempt<3)await uploadWait(500*2**attempt)}}throw lastError}
async function uploadFileInChunks(file,relative,onProgress){
  if(file.size===0){await api(`/api/v1/upload?relativePath=${encode(relative)}`,{method:'POST',headers:{'Content-Type':'application/octet-stream'},body:file});return}
  const uploadId=crypto.randomUUID().replaceAll('-','');
  const status=await api(`/api/v1/uploads/status?uploadId=${uploadId}`),statusData=await status.json();let offset=statusData.offset||0;
  while(offset<file.size){const end=Math.min(file.size,offset+uploadChunkSize),chunk=file.slice(offset,end),start=offset;const response=await withUploadRetry(()=>api(`/api/v1/uploads/chunk?uploadId=${uploadId}&offset=${start}&total=${file.size}&relativePath=${encode(relative)}`,{method:'POST',headers:{'Content-Type':'application/octet-stream'},body:chunk}));const result=await response.json();offset=result.offset;onProgress(offset-start)}
}
async function uploadFilesFast(files,preserveTree){
  if(!files.length)return;const total=files.reduce((sum,file)=>sum+file.size,0);let transferred=0,completed=0,next=0,failed=null;showUploadProgress(0,total,0,files.length);
  const worker=async()=>{while(!failed){const index=next++;if(index>=files.length)return;const file=files[index],relative=preserveTree?joinPath(state.path,file.webkitRelativePath):joinPath(state.path,file.name);try{await uploadFileInChunks(file,relative,bytes=>{transferred+=bytes;showUploadProgress(transferred,total,completed,files.length)});completed++;showUploadProgress(transferred,total,completed,files.length)}catch(error){failed=error}}};
  await Promise.all(Array.from({length:Math.min(3,files.length)},worker));
  try{if(failed)throw failed;toast(`Wysłano ${completed} z ${files.length}`);await refreshAll()}catch(error){toast(`Wysłano ${completed} z ${files.length}. ${error.message}`,true)}finally{setTimeout(()=>$('#uploadStatus').classList.add('hidden'),failed?6000:1800);$('#filePicker').value='';$('#folderPicker').value=''}
}
window.homeCloudUploadFinished=(successful,total,message='')=>{toast(successful===total?`Wysłano ${successful} plików`:`Wysłano ${successful} z ${total}. ${message}`,successful!==total);refreshAll()};

function openNameDialog(mode,entry=null){state.naming={mode,entry};$('#dialogTitle').textContent=mode==='rename'?'Zmień nazwę':'Nowy folder';$('#dialogInput').value=entry?entry.path.split('/').pop():'';$('#dialogConfirm').textContent=mode==='rename'?'Zapisz':'Utwórz';$('#promptDialog').showModal();setTimeout(()=>{$('#dialogInput').focus();$('#dialogInput').select()},50)}
$('#newFolderButton').addEventListener('click',()=>openNameDialog('folder'));
$('#dialogConfirm').addEventListener('click',async event=>{event.preventDefault();const name=$('#dialogInput').value.trim(),job=state.naming;if(!name||!job)return;try{if(job.mode==='rename'){await api(`/api/v1/files/rename?source=${encode(job.entry.path)}&name=${encode(name)}`,{method:'POST'});for(const cache of [state.previewCache,state.thumbnailCache]){const cached=cache.get(job.entry.path);if(cached){URL.revokeObjectURL(cached.url);cache.delete(job.entry.path)}}}else await api(`/api/v1/directories?path=${encode(joinPath(state.path,name))}`,{method:'POST'});$('#promptDialog').close();state.naming=null;toast(job.mode==='rename'?'Nazwa zmieniona':'Folder utworzony');await loadFiles()}catch(error){toast(error.message,true)}});

async function showPreview(entry){const index=state.previewEntries.findIndex(item=>item.path===entry.path);state.previewIndex=index<0?0:index;await showPreviewIndex(state.previewIndex)}
async function showPreviewIndex(index){
  if(!state.previewEntries.length)return;
  state.previewIndex=(index+state.previewEntries.length)%state.previewEntries.length;
  const entry=state.previewEntries[state.previewIndex],dialog=$('#previewDialog'),body=$('#previewBody'),requestId=(state.previewRequest||0)+1;state.previewRequest=requestId;
  $('#previewTitle').textContent=`${entry.path.split('/').pop()}  ·  ${state.previewIndex+1}/${state.previewEntries.length}`;
  body.innerHTML='<span class="preview-loading">Ładowanie…</span>';
  if(!dialog.open)dialog.showModal();
  try{
    const item=await cachedPreview(entry.path),blob=item.blob;
    if(requestId!==state.previewRequest)return;
    body.innerHTML='';
    let viewer;if(blob.type.startsWith('image/')){viewer=document.createElement('img');viewer.alt=entry.path.split('/').pop()}else if(blob.type.startsWith('video/')){viewer=document.createElement('video');viewer.controls=true}else{viewer=document.createElement('iframe');viewer.title=entry.path.split('/').pop()}
    viewer.src=item.url;body.appendChild(viewer);
    const next=state.previewEntries[(state.previewIndex+1)%state.previewEntries.length],previous=state.previewEntries[(state.previewIndex-1+state.previewEntries.length)%state.previewEntries.length];
    setTimeout(()=>cachedPreview(next.path).then(()=>cachedPreview(previous.path)).catch(()=>{}),50);
  }catch(error){body.innerHTML=`<span class="preview-loading">${escapeHtml(error.message)}</span>`}
}
function closePreview(){
  state.previewRequest=(state.previewRequest||0)+1;$('#previewBody').innerHTML='';$('#previewDialog').close();
}
$('#previewClose').addEventListener('click',closePreview);
$('#previewPrevious').addEventListener('click',()=>showPreviewIndex(state.previewIndex-1));
$('#previewNext').addEventListener('click',()=>showPreviewIndex(state.previewIndex+1));
$('#previewDialog').addEventListener('cancel',event=>{event.preventDefault();closePreview()});
$('#previewDialog').addEventListener('click',event=>{if(event.target===$('#previewDialog'))closePreview()});
let previewTouchStart=0;
$('#previewDialog').addEventListener('touchstart',event=>{previewTouchStart=event.changedTouches[0].clientX},{passive:true});
$('#previewDialog').addEventListener('touchend',event=>{const distance=event.changedTouches[0].clientX-previewTouchStart;if(Math.abs(distance)>55)showPreviewIndex(state.previewIndex+(distance<0?1:-1))},{passive:true});
document.addEventListener('keydown',event=>{if(!$('#previewDialog').open)return;if(event.key==='ArrowLeft')showPreviewIndex(state.previewIndex-1);if(event.key==='ArrowRight')showPreviewIndex(state.previewIndex+1)});
window.homeCloudBack=()=>{if($('#previewDialog').open){closePreview();return true}if($('#promptDialog').open){$('#promptDialog').close();return true}if(state.view==='files'&&state.path!=='.'){state.path=parentPath(state.path);loadFiles();return true}if(state.view!=='files'){state.view='files';state.path='.';loadFiles();return true}return false};

async function handleRow(entry,action,trash){
  try{
    if(action==='open'){state.path=entry.path;state.view='files';await loadFiles()}
    else if(action==='preview'){await showPreview(entry)}
    else if(action==='rename'){openNameDialog('rename',entry)}
    else if(action==='download'){
      const response=await api(`/api/v1/download-ticket?path=${encode(entry.path)}`,{method:'POST'});
      const ticket=await response.json(),link=document.createElement('a');
      link.href=ticket.url;link.download=entry.path.split('/').pop();document.body.appendChild(link);link.click();link.remove();
    }else if(action==='trash'){
      if(confirm(`Przenieść „${entry.path.split('/').pop()}” do kosza?`)){
        for(const cache of [state.previewCache,state.thumbnailCache]){const cached=cache.get(entry.path);if(cached){URL.revokeObjectURL(cached.url);cache.delete(entry.path)}}
        await api(`/api/v1/files?path=${encode(entry.path)}`,{method:'DELETE'});
        toast('Przeniesiono do kosza');await refreshAll();
      }
    }else if(action==='restore'){
      await api(`/api/v1/trash/restore?id=${encode(entry.id)}`,{method:'POST'});
      toast('Element przywrócony');await refreshAll();
    }else if(action==='permanent'&&confirm('Usunąć trwale? Tej operacji nie można cofnąć.')){
      await api(`/api/v1/trash?id=${encode(entry.id)}`,{method:'DELETE'});await refreshAll();
    }
  }catch(error){toast(error.message,true)}
}

if(state.token&&state.user)showApp();
