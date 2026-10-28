"use strict";
const fs=require("node:fs");
const path=require("node:path");
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
  const px=485,gap=11,top=103,contentH=705,width=px*3+gap*2,height=866;
  const svg=[
    '<svg xmlns="http://www.w3.org/2000/svg" width="'+width+'" height="'+height+'" viewBox="0 0 '+width+' '+height+'" role="img" aria-label="ios-27 compatibility blocks for system libraries, Darwin and Apple UI">',
    '<rect width="100%" height="100%" fill="#0e1726"/>',
    '<text x="15" y="31" font-family="sans-serif" font-size="26" font-weight="bold" fill="#f3f7fd">ios-27 / system compatibility blocks</text>',
    '<text x="15" y="53" font-family="sans-serif" font-size="14" fill="#adc1d8">each square = 1 declared representative function or contract, NOT the whole Apple API surface</text>'
  ];
  const totals=[];
  for(let pi=0;pi<data.panels.length;pi++){
    const panel=data.panels[pi], x=pi*(px+gap), gs=gather(panel.id);
    const all=gs.flatMap(g=>g.targets),c=count(all);
    totals.push({panel,groups:gs,c,total:all.length});
    svg.push('<rect x="'+x+'" y="67" width="'+px+'" height="749" rx="8" fill="#17263b" stroke="#48617b" stroke-width="1"/>');
    svg.push('<text x="'+(x+11)+'" y="88" font-family="sans-serif" font-size="16" font-weight="bold" fill="#fafafa">'+esc(panel.title)+'</text>');
    svg.push('<text x="'+(x+11)+'" y="104" font-family="sans-serif" font-size="12" fill="#aac8d9">'+c["native-fixture"]+' native / '+c.synthetic+' synthetic / '+all.length+' tracked</text>');
    const order=gs.slice().sort((a,b)=>b.targets.length-a.targets.length||a.id.localeCompare(b.id));
    const columns=[[],[]],loads=[0,0];
    for(const g of order){const col=loads[0]<=loads[1]?0:1;columns[col].push(g);loads[col]+=g.targets.length;}
    for(let col=0;col<2;col++){
      const w=(px-27)/2,cx=x+8+col*(w+10);
      let yy=top+8;const total=loads[col];
      for(let gi=0;gi<columns[col].length;gi++){
        const g=columns[col][gi];
        const isLast=gi===columns[col].length-1;
        const gh=isLast?top+8+contentH-yy:contentH*g.targets.length/total;
        svg.push('<g><title>'+esc(g.label+'; '+g.targets.length+' targets; ceiling: '+g.ceiling+'; '+g.note)+'</title>');
        svg.push('<rect x="'+cx.toFixed(2)+'" y="'+yy.toFixed(2)+'" width="'+w.toFixed(2)+'" height="'+Math.max(gh,0).toFixed(2)+'" fill="#23344c" stroke="#8b9bb1" stroke-width="1"/>');
        const titleH=gh>34?19:0;
        const short=g.label.slice(0,Math.floor((w-8)/7));
        if(titleH)svg.push('<text x="'+(cx+5).toFixed(2)+'" y="'+(yy+14).toFixed(2)+'" font-family="sans-serif" font-size="12" fill="#f4f6fc">'+esc(short)+'</text>');
        const n=g.targets.length, cw=w-4,ch=Math.max(0,gh-titleH-5);
        const ncols=Math.max(1,Math.min(n,Math.ceil(Math.sqrt(n*cw/Math.max(ch,1)))));
        const nrows=Math.ceil(n/ncols), tw=cw/ncols,th=ch/nrows;
        for(let ti=0;ti<n;ti++){
          const t=g.targets[ti],tx=cx+2+(ti%ncols)*tw,ty=yy+titleH+2+Math.floor(ti/ncols)*th;
          svg.push('<g><title>'+esc(g.label+' / '+t.name+': '+names[t.status])+'</title>');
          svg.push('<rect x="'+(tx+.45).toFixed(2)+'" y="'+(ty+.45).toFixed(2)+'" width="'+Math.max(.1,tw-.9).toFixed(2)+'" height="'+Math.max(.1,th-.9).toFixed(2)+'" fill="'+colors[t.status]+'" stroke="#1c2a40" stroke-width=".45"/>');
          svg.push('</g>');
        }
        svg.push('</g>');
        yy+=gh;
      }
    }
    svg.push('<text x="'+(x+11)+'" y="840" font-family="sans-serif" font-size="13" fill="#b4c5d8">'+gs.length+' groups · ceiling categories in docs/BLOCKS.md</text>');
  }
  let lx=15;
  for(const s of ["native-fixture","synthetic","missing","excluded"]){
    svg.push('<rect x="'+lx+'" y="850" width="10" height="10" fill="'+colors[s]+'"/>');
    svg.push('<text x="'+(lx+16)+'" y="860" font-family="sans-serif" font-size="12" fill="#dde6f3">'+esc(names[s])+'</text>');
    lx+=s==="synthetic"?236:s==="native-fixture"?178:s==="missing"?143:220;
  }
  svg.push('</svg>');
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
