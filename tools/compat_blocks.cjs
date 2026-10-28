"use strict";
const fs=require("node:fs");
const path=require("node:path");
const makeMap = function makeMap(data,colors,names,esc,count,gather,live,activity){
  const width=760, margin=18, mapW=724;
  const panels=data.panels.map(panel=>{
    const groups=gather(panel.id);
    const targets=groups.flatMap(g=>g.targets);
    return {panel,groups,targets,h:Math.round(Math.max(230,Math.min(485,166+0.92*targets.length)))};
  });
  const top=100,header=58,gap=24,bottom=70;
  const height=top+panels.reduce((a,p)=>a+header+p.h+gap,0)+bottom;
  const svg=[
    '<svg xmlns="http://www.w3.org/2000/svg" width="'+width+'" height="'+height+'" viewBox="0 0 '+width+' '+height+'" role="img" aria-label="ios-27 tracked API sample with proportional component size and evidence status">',
    '<style>@media(prefers-color-scheme:light){.gh-text{fill:#1f2328}.gh-muted{fill:#59636e}.gh-rule{stroke:#d1d9e0}.gh-surface{fill:#f6f8fa}.gh-missing{fill:#d1d9e0}}</style>',
    '<text x="18" y="33" font-family="sans-serif" font-size="25" font-weight="600" fill="#e6edf3">ios-27 / compatibility</text>',
    '<text x="18" y="55" font-family="sans-serif" font-size="12" fill="#8b949e">component area = tracked targets · color = test evidence</text>'
  ];
  const pending = activity && activity.groups ? activity.groups : {};
  const pendingCount = activity ? activity.candidate_count : 0;
  svg.push('<text x="742" y="81" text-anchor="end" font-family="sans-serif" font-size="12" fill="#a371f7">'+pendingCount+' source-diff candidate(s) · not verified</text>');
  const rect=(x,y,w,h,color,r)=>{
    if(![x,y,w,h].every(Number.isFinite)||w<0||h<0)throw Error("invalid rectangle");
    return '<rect x="'+x.toFixed(2)+'" y="'+y.toFixed(2)+'" width="'+w.toFixed(2)+'" height="'+h.toFixed(2)+'" rx="'+(r||0)+'" fill="'+color+'"/>';
  };
  function partition(items,x,y,w,h){
    const total=items.reduce((a,g)=>a+g.targets.length,0);
    let pending=items.map(g=>({group:g,a:w*h*g.targets.length/total}));
    let row=[],xx=x,yy=y,ww=w,hh=h;
    const result=[];
    const badness=(list,side)=>{
      const sum=list.reduce((a,v)=>a+v.a,0);
      const min=Math.min(...list.map(v=>v.a));
      const max=Math.max(...list.map(v=>v.a));
      return Math.max(side*side*max/(sum*sum),sum*sum/(side*side*min));
    };
    function place(){
      const a=row.reduce((z,item)=>z+item.a,0);
      if(ww>=hh){
        const slice=a/hh;
        let offset=0;
        for(const item of row){
          const ph=item.a/slice;
          result.push({group:item.group,x:xx,y:yy+offset,w:slice,h:ph});
          offset+=ph;
        }
        xx+=slice;ww-=slice;
      }else{
        const slice=a/ww;
        let offset=0;
        for(const item of row){
          const pw=item.a/slice;
          result.push({group:item.group,x:xx+offset,y:yy,w:pw,h:slice});
          offset+=pw;
        }
        yy+=slice;hh-=slice;
      }
      row=[];
    }
    while(pending.length){
      const next=pending[0];
      if(!row.length||badness(row.concat(next),Math.min(ww,hh))<=badness(row,Math.min(ww,hh))){
        row.push(pending.shift());
      }else place();
    }
    if(row.length)place();
    return result;
  }
  let y=top;
  const totals=[];
  for(const p of panels){
    const c=count(p.targets);
    totals.push({panel:p.panel,groups:p.groups,c,total:p.targets.length});
    const covered=c["native-fixture"]+c.synthetic;
    const coverage=(100*covered/p.targets.length).toFixed(1);
    svg.push('<text x="'+margin+'" y="'+(y+19)+'" font-family="sans-serif" font-size="17" font-weight="600" fill="#e6edf3">'+esc(p.panel.title)+'</text>');
    svg.push('<text x="'+(width-margin)+'" y="'+(y+20)+'" text-anchor="end" font-family="sans-serif" font-size="22" font-weight="700" fill="'+(covered?"#e6edf3":"#8b949e")+'">'+coverage+'%</text>');
    svg.push('<text x="'+margin+'" y="'+(y+38)+'" font-family="sans-serif" font-size="12" fill="#8b949e">'+p.groups.length+' components · '+p.targets.length+' tracked</text>');
    svg.push('<text x="'+(width-margin)+'" y="'+(y+39)+'" text-anchor="end" font-family="sans-serif" font-size="12" fill="#8b949e">'+covered+' / '+p.targets.length+' with fixture/probe evidence</text>');
    svg.push(rect(margin,y+46,mapW,4,"#30363d",2));
    if(c["native-fixture"])svg.push(rect(margin,y+46,mapW*c["native-fixture"]/p.targets.length,4,colors["native-fixture"],2));
    if(c.synthetic)svg.push(rect(margin+mapW*c["native-fixture"]/p.targets.length,y+46,mapW*c.synthetic/p.targets.length,4,colors.synthetic,2));
    const mapY=y+header;
    const ordered=p.groups.slice().sort((a,b)=>b.targets.length-a.targets.length||a.id.localeCompare(b.id));
    const placements=partition(ordered,margin,mapY,mapW,p.h);
    let area=0, rendered=0;
    for(const box of placements){
      area+=box.w*box.h;
      if(box.x<margin-0.01||box.y<mapY-0.01||box.x+box.w>margin+mapW+0.01||box.y+box.h>mapY+p.h+0.01)throw Error("component bounds failed");
      const g=box.group,x=box.x+2,y0=box.y+2,w=Math.max(0,box.w-4),h=Math.max(0,box.h-4);
      const wip = Array.isArray(pending[g.id]) ? pending[g.id].length : 0;
      svg.push('<g><title>'+esc(g.label+' · '+g.targets.length+' tracked targets · '+g.ceiling+' ceiling · '+wip+' incoming branches, not counted as compatibility')+'</title>');
      svg.push(rect(x,y0,w,h,"#161b22",3));
      const labelH=(w>82&&h>43)?29:0;
      const iw=Math.max(0,w-4),ih=Math.max(0,h-4-labelH);
      const rows=Math.max(1,Math.min(g.targets.length,Math.round(Math.sqrt(g.targets.length*ih/Math.max(iw,1)))));
      let index=0;
      for(let r=0;r<rows;r++){
        const n=Math.floor(g.targets.length/rows)+(r<g.targets.length%rows?1:0);
        for(let col=0;col<n;col++){
          const t=g.targets[index++];
          const cw=iw/n,ch=ih/rows,tx=x+2+col*cw,ty=y0+2+labelH+r*ch;
          const gutter=Math.min(1.2,cw*.15,ch*.15);
          svg.push('<g><title>'+esc(g.label+' / '+t.name+': '+names[t.status])+'</title>');
          svg.push(rect(tx+gutter/2,ty+gutter/2,Math.max(.1,cw-gutter),Math.max(.1,ch-gutter),colors[t.status],Math.min(1,cw/6,ch/6)));
          svg.push('</g>');
        }
      }
      if(index!==g.targets.length)throw Error("target count mismatch "+g.id);
      rendered+=index;
      if(wip){
        svg.push('<rect x="'+x.toFixed(2)+'" y="'+y0.toFixed(2)+'" width="'+w.toFixed(2)+'" height="'+h.toFixed(2)+'" rx="3" fill="none" stroke="#a371f7" stroke-width="2" stroke-dasharray="5 3"/>');
        if(w>110)svg.push('<text x="'+(x+w-7).toFixed(2)+'" y="'+(y0+h-7).toFixed(2)+'" text-anchor="end" font-family="sans-serif" font-size="11" fill="#c9a7ff">'+wip+' WIP branch'+(wip===1?'':'es')+'</text>');
      }
      if(labelH){
        const evidence=g.targets.filter(t=>t.status==="native-fixture"||t.status==="synthetic").length;
        const showPercent=w>=155;
        const maxChars=Math.max(3,Math.floor((w-(showPercent?52:12))/6.1));
        const label=g.label.length>maxChars?g.label.slice(0,maxChars-1)+"…":g.label;
        svg.push('<text x="'+(x+6).toFixed(2)+'" y="'+(y0+15).toFixed(2)+'" font-family="sans-serif" font-size="11.5" font-weight="500" fill="#c9d1d9">'+esc(label)+'</text>');
        if(w>=105){
          const green=g.targets.filter(t=>t.status==="native-fixture").length;
          const amber=g.targets.filter(t=>t.status==="synthetic").length;
          svg.push(rect(x+6,y0+22,w-12,2,"#30363d",1));
          if(green)svg.push(rect(x+6,y0+22,(w-12)*green/g.targets.length,2,colors["native-fixture"],1));
          if(amber)svg.push(rect(x+6+(w-12)*green/g.targets.length,y0+22,(w-12)*amber/g.targets.length,2,colors.synthetic,1));
        }
        if(showPercent){
          svg.push('<text x="'+(x+w-7).toFixed(2)+'" y="'+(y0+15).toFixed(2)+'" text-anchor="end" font-family="sans-serif" font-size="11.5" font-weight="600" fill="'+(evidence?"#d29922":"#8b949e")+'">'+Math.round(100*evidence/g.targets.length)+'%</text>');
        }
      }
      svg.push('</g>');
    }
    if(Math.abs(area-mapW*p.h)>0.05||rendered!==p.targets.length)throw Error("proportional area accounting error");
    y+=header+p.h+gap;
  }
  const legendY=height-40;
  svg.push('<line x1="18" x2="742" y1="'+(legendY-20)+'" y2="'+(legendY-20)+'" stroke="#30363d" stroke-width="1"/>');
  let lx=18;
  for(const [status,title,advance] of [
    ["native-fixture","native fixture",139],
    ["synthetic","synthetic model/probe",210],
    ["missing","not implemented",182],
    ["excluded","excluded",130]
  ]){
    svg.push(rect(lx,legendY-3,10,10,colors[status],2));
    svg.push('<text x="'+(lx+16)+'" y="'+(legendY+6)+'" font-family="sans-serif" font-size="11.5" fill="#8b949e">'+esc(title)+'</text>');
    lx+=advance;
  }
  const themed=svg.map(line=>line
    .replace(/fill="#e6edf3"/g,'fill="#e6edf3" class="gh-text"')
    .replace(/fill="#c9d1d9"/g,'fill="#c9d1d9" class="gh-text"')
    .replace(/fill="#8b949e"/g,'fill="#8b949e" class="gh-muted"')
    .replace(/stroke="#30363d"/g,'stroke="#30363d" class="gh-rule"')
    .replace(/fill="#161b22"/g,'fill="#161b22" class="gh-surface"')
    .replace(/fill="#30363d"/g,'fill="#30363d" class="gh-missing"'));
  themed.push('</svg>');
  return {svg:themed,totals};
};
const render = function render(data,live,activity){
  "use strict";
  const esc=s=>String(s).replace(/&/g,"&amp;").replace(/</g,"&lt;").replace(/>/g,"&gt;").replace(/"/g,"&quot;").replace(/'/g,"&#39;");
  const colors={"native-fixture":"#3fb950","synthetic":"#d29922","missing":"#30363d","excluded":"#b85c64"};
  const names={"native-fixture":"native arm64 fixture","synthetic":"synthetic parser/model/probe","missing":"unimplemented","excluded":"outside userspace-only scope"};
  const classes={plausible:"limited subset feasible",conditional:"needs major compatibility layer",unknown:"official Apple compatibility unproven",excluded:"intentionally not in scope"};
  const count=items=>{const o={"native-fixture":0,synthetic:0,missing:0,excluded:0};for(const t of items)o[t.status]++;return o;};
  const gather=p=>data.groups.filter(g=>g.panel===p);
  if(data.schema!==1||data.panels.length!==3||!data.scope)throw Error("bad catalog");
  const panelNames=new Set(data.panels.map(p=>p.id)),groupNames=new Set();
  for(const g of data.groups){
    if(!panelNames.has(g.panel)||groupNames.has(g.id)||!classes[g.ceiling]||!g.note||!Array.isArray(g.targets)||!g.targets.length)throw Error("invalid group "+g.id);
    groupNames.add(g.id);const seen=new Set();
    for(const t of g.targets){if(!t.name||seen.has(t.name)||!(t.status in names))throw Error("invalid target "+g.id+" "+t.name);seen.add(t.name);}
    if(g.targets.some(t=>t.status==="native-fixture"||t.status==="synthetic")&&(!g.evidence||!(g.test||g.standalone_test)))throw Error("unproven status "+g.id);
  }
  const {svg,totals}=makeMap(data,colors,names,esc,count,gather,live,activity);
  const md=[
    "# ios-27 | API/function block map and feasibility ceilings","",
    "![per-function compatibility block map](blocks.svg)","",
    "**how to read:** each block is one explicitly enumerated **sample target**, not every function exported by iOS. "
      +"green = native ARM64 test fixture only; amber = parser, bounded synthetic Mach model or syscall interpreter probe; "
      +"grey = not implemented; red = expressly excluded by userspace-only policy. "
      +"No color means that a real Apple framework or application is runnable.","",
    "[Browsable per-target details](blocks.html) · [Machine-readable sample catalog](compat-targets.json) · [Broader milestones](PROGRESS.md) · [Audited ceilings](CEILINGS.md) · [Incoming agent work](BRANCHES.md)","",
    "The displayed percentages are **sample evidence coverage**: native fixtures plus synthetic probes divided by explicitly tracked targets, not real iOS framework compatibility.","",
    "## counted target samples","",
    "| layer | native fixtures | synthetic/probe | unimplemented | excluded | sampled targets |",
    "| --- | ---: | ---: | ---: | ---: | ---: |"
  ];
  for(const item of totals){
    md.push("| **"+item.panel.title+"** | **"+item.c["native-fixture"]+"** | **"+item.c.synthetic+"** | **"+item.c.missing+"** | **"+item.c.excluded+"** | **"+item.total+"** |");
    for(const g of item.groups){const c=count(g.targets);md.push("| ↳ "+g.label+" | "+c["native-fixture"]+" | "+c.synthetic+" | "+c.missing+" | "+c.excluded+" | "+g.targets.length+" |");}
  }
  md.push("","## merged main and incoming work","",
    "confirmed evidence is drawn only from merged main. the [live provenance snapshot](repo-status.json) records recent branches, which never count until merged and reviewed.","",
    "main source: `"+(live && live.main_source?live.main_source.sha.slice(0,12):"unavailable")+"`","");
  if(live && Array.isArray(live.open_pull_requests)){
    for(const p of live.open_pull_requests.slice(0,8)){
      const n=Number(p.number);
      if(Number.isSafeInteger(n)&&n>0)md.push("- [PR #"+n+"](https://github.com/kallmek3nny/ios-27/pull/"+n+") · incoming, not counted");
    }
  }
  md.push("","## parsed branch diffs (provisional, not merged)","",
    "the purple dashed outlines identify components with new source or test changes on tracked agent branches. they do not increase verified compatibility percentages.","",
    "[Live branch diff inventory and CI evidence](BRANCHES.md) · "+(activity?activity.candidate_count:0)+" active candidate(s) with recognized code changes.","");
  for(const g of data.groups){
    const work=activity&&activity.groups?activity.groups[g.id]:null;
    if(Array.isArray(work)&&work.length)md.push("- **"+g.label+"**: "+work.length+" unmerged candidate branch(es), not counted");
  }
  md.push("","## realistic ceilings for every tracked area","","[Why these ceilings are not numerical guarantees](CEILINGS.md).","");
  for(const item of totals){
    md.push("### "+item.panel.title,"");
    for(const g of item.groups)md.push("- **"+g.label+"** · **"+g.ceiling+"** ("+classes[g.ceiling]+"). "+g.note);
    md.push("");
  }
  md.push("## counting rules and barriers","",
    "- **not an exhaustive total:** the catalog is a selected, versioned set of functions and contracts. no complete apple sdk/export baseline was scanned, so do not call these percentages of system libraries translated.",
    "- **no fake implementation:** a symbol stub, parser match, green unit test or constant return cannot count as real Apple ABI support. each amber/green group requires a real evidence path and registered test; promotion needs meaningful end-to-end proof.",
    "- **native-fixture is NOT general iOS execution:** actual arm64 machine code can run with a narrow test-only ABI, but real SpringBoard, framework processes, full Darwin traps and Apple private services do not.",
    "- **syscall distinction:** seven selector behaviors in the interpreter are counted as synthetic probes, not functioning libsystem calls callable by real apps.",
    "- **ceiling does not mean a percentage:** plausible = limited userspace subset technically possible; conditional = major unbuilt compatibility components; unknown = no evidence of authentic Apple behavior; excluded = would violate established kernel/boot/device restrictions.",
    "- **update:** edit docs/compat-targets.json and run a bounded command: timeout 15s node tools/compat_blocks.cjs. verify output with timeout 15s node tools/compat_blocks.cjs --check.",
    "- **safety:** no bootloader or verified-boot changes, kernel replacement, persistent root, Apple proprietary redistribution, or running untrusted binaries in the in-process fixture runner.","",
    "## upstream target inventories and context",""
  );
  for(const s of data.source_references)md.push("- ["+s.name+"]("+s.url+")");
  md.push("","snapshot date: "+data.as_of+". no historical API counts or guaranteed numeric ceiling invented.","");
  const html=[
    '<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">',
    '<title>ios-27 / compatibility block explorer</title>',
    '<style>body{margin:0 auto;max-width:1540px;padding:24px;background:#0d1117;color:#c9d1d9;font:15px system-ui,sans-serif}a{color:#58a6ff}p{line-height:1.55}.lead{color:#8b949e;max-width:100ch}.map{width:100%;height:auto;display:block;border-radius:10px}.panels{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(100%,365px),1fr));gap:15px}.panel{background:#161b22;border:1px solid #30363d;border-radius:10px;padding:12px}.group{border:1px solid #30363d;border-radius:7px;margin:9px 0}.group summary{padding:10px;cursor:pointer}.group p{padding:0 10px;color:#8b949e;font-size:13px}.chips{display:grid;grid-template-columns:repeat(auto-fit,minmax(145px,1fr));gap:6px;padding:10px}.chip{background:#21262d;border-radius:4px;display:flex;gap:6px;align-items:flex-start;padding:5px;font-size:12px;overflow-wrap:anywhere}.sq{display:inline-block;width:10px;height:10px;min-width:10px;border-radius:2px;margin-top:2px}.stats{font-size:12px;color:#8b949e}.tag{font-size:11px;border:1px solid #30363d;padding:2px 5px;border-radius:99px;color:#c9d1d9}</style>',
    '</head><body><h1>ios-27 / block compatibility explorer</h1>',
    '<p class="lead">anyps5-style visualization for deliberately enumerated target api functions and contracts. representative target sample, not all iOS exports. native means a restricted ARM64 test fixture; synthetic means a limited parser/probe/model. neither proves a working Apple app. no exact completion ceiling can be derived for Apple framework layers.</p>',
    '<img class="map" src="blocks.svg" alt="system libraries and iOS runtime target block map">',
    '<p class="lead">purple dashed outlines mark active unmerged source diffs, not validated API translations. <a href="BRANCHES.md">inspect agent branches and changed files</a>. expand components for details.</p>',
    '<div class="panels">'
  ];
  for(const it of totals){
    html.push('<section class="panel"><h2>'+esc(it.panel.title)+'</h2><p class="lead">'+esc(it.panel.subtitle)+'</p>');
    html.push('<p class="stats">'+it.total+' targets · '+it.c["native-fixture"]+' native · '+it.c.synthetic+' synthetic · '+it.c.missing+' missing · '+it.c.excluded+' excluded</p>');
    for(const g of it.groups){
      const c=count(g.targets);
      const work=activity && activity.groups && Array.isArray(activity.groups[g.id]) ? activity.groups[g.id] : [];
      html.push('<details class="group"><summary><strong>'+esc(g.label)+'</strong> <span class="tag">'+esc(g.ceiling)+' ceiling</span> <span class="stats">'+c["native-fixture"]+' native, '+c.synthetic+' synthetic, '+g.targets.length+' targets</span>'+(work.length?' <span style="color:#a371f7">'+work.length+' incoming</span>':'')+'</summary>');
      for(const candidate of work){
        const href=String(candidate.url);
        if(href.startsWith('https://github.com/kallmek3nny/ios-27/'))html.push('<p class="description"><a href="'+esc(href)+'">'+esc(candidate.branch)+'</a> · '+candidate.source_files+' source, '+candidate.fixture_files+' fixture files · CI: '+esc(candidate.ci)+' · not yet merged</p>');
      }
      html.push('<p>'+esc(g.note)+'</p>');
      if(g.evidence)html.push('<p>evidence: <a href="../'+esc(g.evidence)+'">'+esc(g.evidence)+'</a> · test: '+esc(g.test)+'</p>');
      html.push('<div class="chips">');
      for(const t of g.targets)html.push('<div class="chip" title="'+esc(names[t.status])+'"><span class="sq" style="background:'+colors[t.status]+'"></span><span>'+esc(t.name)+'</span></div>');
      html.push('</div></details>');
    }
    html.push('</section>');
  }
  html.push('</div><p class="lead">snapshot '+esc(data.as_of)+'. no real Apple frameworks, SpringBoard, or UI demonstrated. <a href="PROGRESS.md">research milestone progress</a>.</p></body></html>');
  const branches=[
    "# ios-27 | live agent branch source diffs","",
    "![proportional API blocks with purple WIP markers](blocks.svg)","",
    "**purple outlines = changed source or test files on an unmerged branch.** "+
    "these are *provisional change signals*, not implementations that passed an Apple ABI conformance test. "+
    "the verified green/amber counts remain strictly controlled by reviewed main catalogs.","",
    "candidate branches with mapped changes: **"+(activity?activity.candidate_count:0)+"** · "+
    "audited base: `"+(activity?activity.main_source_sha.slice(0,12):"unknown")+"`",""
  ];
  if(!activity||!activity.candidates.length){
    branches.push("no unmerged branches currently have source or test changes matching the catalog's known subsystem paths. this does **not** mean older work is absent from main.","");
  } else for(const cand of activity.candidates){
    const branch=String(cand.branch).replace(/[^a-zA-Z0-9_./-]/g,"");
    const target=cand.pr_number?"https://github.com/kallmek3nny/ios-27/pull/"+Number(cand.pr_number):"https://github.com/kallmek3nny/ios-27/tree/"+encodeURIComponent(branch);
    branches.push("### ["+branch+"]("+target+")","",
      "head: `"+cand.sha.slice(0,12)+"` · ci: **"+cand.ci+"** · unmerged, not credited.","");
    for(const item of cand.groups){
      const group=data.groups.find(g=>g.id===item.group_id);
      const label=group?group.label:item.group_id;
      branches.push("- **"+label+"**: "+item.source_files+" source files, "+item.fixture_files+
        " test/fixture files, +"+item.additions+"/-"+item.deletions+" lines");
      if(item.symbols_preview.length)branches.push("  - candidate declarations: `"+item.symbols_preview.join("`, `")+"`");
    }
    branches.push("");
  }
  branches.push("## verification policy","",
    "changed paths and added declarations are parsed from GitHub PR file patches or main-vs-branch diffs, not commit messages. "+
    "CI status is a hint only. staged branches may fail integration, omit needed tests, or duplicate already merged work. "+
    "only separately reviewed catalog updates from merged main can change confirmed target statuses.","",
    "Refresh: `timeout 150s python3 tools/scan_branch_activity.py`, then `timeout 15s node tools/compat_blocks.cjs`. "+
    "a scheduled workflow checks for new agent work at least every 15 minutes, subject to GitHub Actions scheduling delays.","");
  return {svg:svg.join("\n")+"\n",html:html.join("\n")+"\n",md:md.join("\n")+"\n",
          branches:branches.join("\n")+"\n",
          counts:totals.map(x=>({id:x.panel.id,total:x.total,...x.c}))};
};
const root=path.resolve(__dirname,"..");
const raw=fs.readFileSync(path.join(root,"docs/compat-targets.json"),"utf8");
const data=JSON.parse(raw);
const cmake=fs.readFileSync(path.join(root,"CMakeLists.txt"),"utf8");
for(const g of data.groups){if(g.targets.some(t=>t.status==="native-fixture"||t.status==="synthetic")){
 if(!g.evidence||g.evidence.startsWith("/")||g.evidence.includes("..")||!fs.existsSync(path.join(root,g.evidence)))throw Error("missing evidence for "+g.id);
 if(g.standalone_test){if(g.standalone_test!=="tools/phase19e-standalone.py"||!fs.existsSync(path.join(root,g.standalone_test)))throw Error("standalone harness missing for "+g.id);continue;}
 if(!g.test)throw Error("missing CTest name for "+g.id);
 if(!new RegExp("add_test\\s*\\(\\s*NAME\\s+"+g.test.replace(/[.*+?^${}()|[\]\\]/g,"\\$&")+"(?:\\s|\\))").test(cmake))throw Error("missing registered CTest for "+g.id+" "+g.test);
}}
const liveFile=path.join(root,"docs/repo-status.json");
const live=fs.existsSync(liveFile)?JSON.parse(fs.readFileSync(liveFile,"utf8")):null;
if(live&&(live.schema!==1||!live.main_source||!/^[0-9a-f]{40}$/.test(live.main_source.sha)||!Array.isArray(live.open_pull_requests)))throw Error("invalid repo provenance");
const activityPath=path.join(root,"docs/branch-activity.json");
const activity=JSON.parse(fs.readFileSync(activityPath,"utf8"));
if(activity.schema!==1||!Array.isArray(activity.candidates)||!activity.groups||typeof activity.candidate_count!=="number"||!/^[a-f0-9]{40}$/.test(activity.main_source_sha))throw Error("invalid candidate branch snapshot");
const result=render(data,live,activity);
const files=[["docs/blocks.svg",result.svg],["docs/blocks.html",result.html],["docs/BLOCKS.md",result.md],["docs/BRANCHES.md",result.branches]];
if(process.argv.includes("--check")){const stale=files.filter(([name,content])=>{const file=path.join(root,name);return !fs.existsSync(file)||fs.readFileSync(file,"utf8")!==content;}).map(([name])=>name);if(stale.length){process.stderr.write("stale block outputs: "+stale.join(", ")+"\n");process.exit(1);}}
else {for(const [name,content] of files)fs.writeFileSync(path.join(root,name),content,"utf8");}
process.stdout.write("compat blocks verified: "+result.counts.map(c=>c.id+" "+c.total).join(", ")+"\n");
