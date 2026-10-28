"use strict";
const fs=require("node:fs");
const path=require("node:path");
const makeMap = function makeMap(data,colors,names,esc,count,gather){
  const width=760, margin=18, mapW=724;
  const panels=data.panels.map(panel=>{
    const groups=gather(panel.id);
    const targets=groups.flatMap(g=>g.targets);
    return {panel,groups,targets,h:Math.round(Math.max(230,Math.min(485,166+0.92*targets.length)))};
  });
  const top=84,header=42,gap=22,bottom=67;
  const height=top+panels.reduce((a,p)=>a+header+p.h+gap,0)+bottom;
  const svg=[
    '<svg xmlns="http://www.w3.org/2000/svg" width="'+width+'" height="'+height+'" viewBox="0 0 '+width+' '+height+'" role="img" aria-label="proportional ios-27 compatibility blocks">',
    '<rect width="'+width+'" height="'+height+'" fill="#0e1724"/>',
    '<text x="18" y="33" font-family="sans-serif" font-size="25" font-weight="600" fill="#f0f5fa">ios-27 / compatibility</text>',
    '<text x="18" y="55" font-family="sans-serif" font-size="12" fill="#9cacc0">component area scales with its tracked functions; colors show test status</text>'
  ];
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
    svg.push('<text x="'+margin+'" y="'+(y+15)+'" font-family="sans-serif" font-size="17" font-weight="600" fill="#e7edf4">'+esc(p.panel.title)+'</text>');
    svg.push('<text x="'+(width-margin)+'" y="'+(y+15)+'" text-anchor="end" font-family="sans-serif" font-size="12" fill="#9db0c4">'+p.groups.length+' components · '+p.targets.length+' targets</text>');
    svg.push('<line x1="'+margin+'" x2="'+(width-margin)+'" y1="'+(y+30)+'" y2="'+(y+30)+'" stroke="#2c3a4b" stroke-width="1"/>');
    const mapY=y+header;
    const ordered=p.groups.slice().sort((a,b)=>b.targets.length-a.targets.length||a.id.localeCompare(b.id));
    const placements=partition(ordered,margin,mapY,mapW,p.h);
    let area=0, rendered=0;
    for(const box of placements){
      area+=box.w*box.h;
      if(box.x<margin-0.01||box.y<mapY-0.01||box.x+box.w>margin+mapW+0.01||box.y+box.h>mapY+p.h+0.01)throw Error("component bounds failed");
      const g=box.group,x=box.x+2,y0=box.y+2,w=Math.max(0,box.w-4),h=Math.max(0,box.h-4);
      svg.push('<g><title>'+esc(g.label+' · '+g.targets.length+' targets · '+g.ceiling+' ceiling')+'</title>');
      svg.push(rect(x,y0,w,h,"#1b293b",2));
      const labelH=(w>82&&h>38)?22:0;
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
      if(labelH){
        const maxChars=Math.floor((w-12)/6.1);
        const label=g.label.length>maxChars?g.label.slice(0,maxChars-1)+"…":g.label;
        svg.push('<text x="'+(x+6).toFixed(2)+'" y="'+(y0+15).toFixed(2)+'" font-family="sans-serif" font-size="11.5" fill="#dee7f2">'+esc(label)+'</text>');
      }
      svg.push('</g>');
    }
    if(Math.abs(area-mapW*p.h)>0.05||rendered!==p.targets.length)throw Error("proportional area accounting error");
    y+=header+p.h+gap;
  }
  const legendY=height-40;
  svg.push('<line x1="18" x2="742" y1="'+(legendY-20)+'" y2="'+(legendY-20)+'" stroke="#2c3a4b" stroke-width="1"/>');
  let lx=18;
  for(const [status,title,advance] of [
    ["native-fixture","native fixture",139],
    ["synthetic","synthetic model/probe",210],
    ["missing","not implemented",182],
    ["excluded","excluded",130]
  ]){
    svg.push(rect(lx,legendY-3,10,10,colors[status],2));
    svg.push('<text x="'+(lx+16)+'" y="'+(legendY+6)+'" font-family="sans-serif" font-size="11.5" fill="#c3d0e0">'+esc(title)+'</text>');
    lx+=advance;
  }
  svg.push('</svg>');
  return {svg,totals};
};
const render = function render(data){
  "use strict";
  const esc=s=>String(s).replace(/&/g,"&amp;").replace(/</g,"&lt;").replace(/>/g,"&gt;").replace(/"/g,"&quot;").replace(/'/g,"&#39;");
  const colors={"native-fixture":"#32d5ad","synthetic":"#edbc59","missing":"#526175","excluded":"#ac4b58"};
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
    if(g.targets.some(t=>t.status==="native-fixture"||t.status==="synthetic")&&(!g.evidence||!g.test))throw Error("unproven status "+g.id);
  }
  const {svg,totals}=makeMap(data,colors,names,esc,count,gather);
  const md=[
    "# ios-27 | API/function block map and feasibility ceilings","",
    "![per-function compatibility block map](blocks.svg)","",
    "**how to read:** each block is one explicitly enumerated **sample target**, not every function exported by iOS. "
      +"green = native ARM64 test fixture only; amber = parser, bounded synthetic Mach model or syscall interpreter probe; "
      +"grey = not implemented; red = expressly excluded by userspace-only policy. "
      +"No color means that a real Apple framework or application is runnable.","",
    "[Browsable per-target details](blocks.html) · [Machine-readable sample catalog](compat-targets.json) · [Broader milestones](PROGRESS.md)","",
    "## counted target samples","",
    "| layer | native fixtures | synthetic/probe | unimplemented | excluded | sampled targets |",
    "| --- | ---: | ---: | ---: | ---: | ---: |"
  ];
  for(const item of totals){
    md.push("| **"+item.panel.title+"** | **"+item.c["native-fixture"]+"** | **"+item.c.synthetic+"** | **"+item.c.missing+"** | **"+item.c.excluded+"** | **"+item.total+"** |");
    for(const g of item.groups){const c=count(g.targets);md.push("| ↳ "+g.label+" | "+c["native-fixture"]+" | "+c.synthetic+" | "+c.missing+" | "+c.excluded+" | "+g.targets.length+" |");}
  }
  md.push("","## realistic ceilings for every tracked area","");
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
    '<style>body{margin:0 auto;max-width:1540px;padding:24px;background:#0f1828;color:#e6edf7;font:15px system-ui,sans-serif}a{color:#91c5ff}p{line-height:1.55}.lead{color:#b4c4d8;max-width:100ch}.map{width:100%;height:auto;display:block;border-radius:10px}.panels{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(100%,365px),1fr));gap:15px}.panel{background:#1a293c;border:1px solid #455570;border-radius:10px;padding:12px}.group{border:1px solid #496078;border-radius:7px;margin:9px 0}.group summary{padding:10px;cursor:pointer}.group p{padding:0 10px;color:#b7c9db;font-size:13px}.chips{display:grid;grid-template-columns:repeat(auto-fit,minmax(145px,1fr));gap:6px;padding:10px}.chip{background:#283a51;border-radius:4px;display:flex;gap:6px;align-items:flex-start;padding:5px;font-size:12px;overflow-wrap:anywhere}.sq{display:inline-block;width:10px;height:10px;min-width:10px;border-radius:2px;margin-top:2px}.stats{font-size:12px;color:#b7c9db}.tag{font-size:11px;border:1px solid #7589a0;padding:2px 5px;border-radius:99px;color:#d1e0f1}</style>',
    '</head><body><h1>ios-27 / block compatibility explorer</h1>',
    '<p class="lead">anyps5-style visualization for deliberately enumerated target api functions and contracts. representative target sample, not all iOS exports. native means a restricted ARM64 test fixture; synthetic means a limited parser/probe/model. neither proves a working Apple app. no exact completion ceiling can be derived for Apple framework layers.</p>',
    '<img class="map" src="blocks.svg" alt="system libraries and iOS runtime target block map">',
    '<p class="lead">expand any library or subsystem to inspect every tracked function, its current status and the qualitative feasibility ceiling. <a href="BLOCKS.md">counting rules and roadmap</a></p>',
    '<div class="panels">'
  ];
  for(const it of totals){
    html.push('<section class="panel"><h2>'+esc(it.panel.title)+'</h2><p class="lead">'+esc(it.panel.subtitle)+'</p>');
    html.push('<p class="stats">'+it.total+' targets · '+it.c["native-fixture"]+' native · '+it.c.synthetic+' synthetic · '+it.c.missing+' missing · '+it.c.excluded+' excluded</p>');
    for(const g of it.groups){
      const c=count(g.targets);
      html.push('<details class="group"><summary><strong>'+esc(g.label)+'</strong> <span class="tag">'+esc(g.ceiling)+' ceiling</span> <span class="stats">'+c["native-fixture"]+' native, '+c.synthetic+' synthetic, '+g.targets.length+' targets</span></summary>');
      html.push('<p>'+esc(g.note)+'</p>');
      if(g.evidence)html.push('<p>evidence: <a href="../'+esc(g.evidence)+'">'+esc(g.evidence)+'</a> · test: '+esc(g.test)+'</p>');
      html.push('<div class="chips">');
      for(const t of g.targets)html.push('<div class="chip" title="'+esc(names[t.status])+'"><span class="sq" style="background:'+colors[t.status]+'"></span><span>'+esc(t.name)+'</span></div>');
      html.push('</div></details>');
    }
    html.push('</section>');
  }
  html.push('</div><p class="lead">snapshot '+esc(data.as_of)+'. no real Apple frameworks, SpringBoard, or UI demonstrated. <a href="PROGRESS.md">research milestone progress</a>.</p></body></html>');
  return {svg:svg.join("\n")+"\n",html:html.join("\n")+"\n",md:md.join("\n")+"\n",counts:totals.map(x=>({id:x.panel.id,total:x.total,...x.c}))};
};
const root=path.resolve(__dirname,"..");
const raw=fs.readFileSync(path.join(root,"docs/compat-targets.json"),"utf8");
const data=JSON.parse(raw);
const cmake=fs.readFileSync(path.join(root,"CMakeLists.txt"),"utf8");
for(const g of data.groups){if(g.targets.some(t=>t.status==="native-fixture"||t.status==="synthetic")){
 if(!g.evidence||!g.test||g.evidence.startsWith("/")||g.evidence.includes("..")||!fs.existsSync(path.join(root,g.evidence)))throw Error("missing evidence for "+g.id);
 if(!new RegExp("add_test\\s*\\(\\s*NAME\\s+"+g.test.replace(/[.*+?^${}()|[\]\\]/g,"\\$&")+"(?:\\s|\\))").test(cmake))throw Error("missing registered CTest for "+g.id+" "+g.test);
}}
const result=render(data);
const files=[["docs/blocks.svg",result.svg],["docs/blocks.html",result.html],["docs/BLOCKS.md",result.md]];
if(process.argv.includes("--check")){const stale=files.filter(([name,content])=>{const file=path.join(root,name);return !fs.existsSync(file)||fs.readFileSync(file,"utf8")!==content;}).map(([name])=>name);if(stale.length){process.stderr.write("stale block outputs: "+stale.join(", ")+"\n");process.exit(1);}}
else {for(const [name,content] of files)fs.writeFileSync(path.join(root,name),content,"utf8");}
process.stdout.write("compat blocks verified: "+result.counts.map(c=>c.id+" "+c.total).join(", ")+"\n");
