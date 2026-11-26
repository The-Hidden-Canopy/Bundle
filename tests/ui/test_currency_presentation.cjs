const fs=require('fs'),vm=require('vm'),assert=require('assert');
const path=require('path');
const source=fs.readFileSync(process.argv[2] || path.join(__dirname,'../../ui/bundle_ui/static/app.js'),'utf8');
const a=source.indexOf('  const includedCurrency = '),b=source.indexOf('  const storeKey = ',a);
assert(a>=0&&b>a);
const state={snapshot:{accounts:[{currency:'JPY',status:'active',include_in_safe_to_spend:0},{currency:'USD',status:'active',include_in_safe_to_spend:1}]}};
const ctx={state,esc:x=>x,money:(x,c)=>c+':'+x};
const helpers=vm.runInNewContext(source.slice(a,b)+'\n({currencyOf,currencyTotals,totalsLabel,previewTotalsLabel,validSavedFloor})',ctx);
assert.equal(helpers.currencyOf(),'USD');
state.calc={receipt:{outputs:{currency:'EUR'}}}; assert.equal(helpers.currencyOf(),'EUR');
const totals=helpers.currencyTotals([{currency:'USD',amount:500},{currency:'JPY',amount:700},{currency:'USD',amount:250}],r=>r.amount);
assert.equal(JSON.stringify(totals),'[["JPY",700],["USD",750]]');
assert.equal(helpers.totalsLabel(totals),'JPY JPY:700 / USD USD:750');
assert.equal(helpers.totalsLabel(helpers.currencyTotals([{currency:'USD',amount:Number.MAX_SAFE_INTEGER},{currency:'USD',amount:1},{currency:'USD',amount:1}],r=>r.amount)),'USD unavailable');
assert.equal(helpers.totalsLabel([]),'None');
assert(source.includes('currencyTotals(s.accounts, a => a.available_minor)'));
assert(source.includes('g.currency === currencyOf()'));
assert(source.includes('currency: g.currency'));
assert(source.includes('previewTotalsLabel(p, "debits_minor")'));
assert(source.includes('previewTotalsLabel(p, "credits_minor")'));
const sample=[{currency:'JPY',amount_minor:1,direction:'debit'}];
assert.equal(helpers.previewTotalsLabel({transaction_count:1,sample,debits_minor:1},'debits_minor'),'JPY JPY:1');
for(const p of [{},{transaction_count:2,sample,debits_minor:1},{transaction_count:1,sample,debits_minor:9},{transaction_count:1,sample,debits_minor:'1'},{transaction_count:2,sample:[...sample,{currency:'USD',amount_minor:1,direction:'debit'}],debits_minor:2}])
 assert.equal(helpers.previewTotalsLabel(p,'debits_minor'),'Unavailable; preview again');
assert.equal(helpers.previewTotalsLabel({currency_totals:[{currency:'USD',debits_minor:1},{currency:'JPY',debits_minor:2}]},'debits_minor'),'USD USD:1 / JPY JPY:2');
assert.equal(helpers.previewTotalsLabel({currency_totals:[null]},'debits_minor'),'Unavailable');
assert.equal(helpers.previewTotalsLabel({currency_totals:[{currency:'USD'}]},'debits_minor'),'USD unavailable');
assert(helpers.validSavedFloor({currency:'USD',floor_minor:0},state.snapshot));
assert(!helpers.validSavedFloor({currency:'JPY',floor_minor:100},state.snapshot));
assert(!helpers.validSavedFloor({floor_minor:100},state.snapshot));
assert(!helpers.validSavedFloor({currency:'USD',floor_minor:Infinity},state.snapshot));
const pstart=source.indexOf('  function protectedProgress()'), pend=source.indexOf('  function jarStages',pstart);
state.calc=null;
state.snapshot.goals=[{protected:1,status:'active',currency:'USD',current_reserved_minor:Number.MAX_SAFE_INTEGER,target_minor:100},{protected:1,status:'active',currency:'USD',current_reserved_minor:1,target_minor:100}];
const progress=vm.runInNewContext(source.slice(pstart,pend)+'\nprotectedProgress()', {state,currencyOf:helpers.currencyOf,currencyTotals:helpers.currencyTotals,stageFor:()=>0});
assert.equal(progress.reserved,null);
assert.equal(progress.stage,null);
console.log('Currency partition, selection precedence, overflow refusal and UI wiring checks passed');

const renderStart=source.indexOf("  function renderImport()"),renderEnd=source.indexOf("  function guideSteps",renderStart);
state.workspace={backups:[],backups_directory:"synthetic"}; state.snapshot.export_blockers=[]; state.lastAction=null;
const renderImport=vm.runInNewContext(source.slice(renderStart,renderEnd)+"\nrenderImport",{
 state,esc:x=>String(x),fmtDate:x=>String(x),fmtDateTime:x=>String(x),title:x=>String(x),money:(x,c)=>c+":"+x,
 previewTotalsLabel:helpers.previewTotalsLabel,pageHead:x=>x
});
const base={filename:"synthetic.csv",format:"csv",accounts:[],transaction_count:1,balance_count:0,source_id:"synthetic",sample};
state.preview={...base,debits_minor:1,credits_minor:0};
assert(renderImport().includes("JPY JPY:1 / JPY JPY:0"));
state.preview={...base,debits_minor:99,credits_minor:null};
assert(renderImport().includes("Unavailable; preview again / Unavailable; preview again"));
state.preview={...base,currency_totals:[{currency:"JPY",debits_minor:1,credits_minor:0}]};
assert(renderImport().includes("JPY JPY:1 / JPY JPY:0"));
console.log("Actual import renderer accepts verified legacy/new responses and rejects malformed legacy amounts");
const calcStart=source.indexOf("  async function loadState()"),calcEnd=source.indexOf("  async function refresh()",calcStart);
const floorState={snapshot:null,calc:null,calcError:null}; let requests=[];
const api=async url=>{requests.push(url); if(url==="/api/state")return {snapshot:{accounts:[{currency:"USD",status:"active",include_in_safe_to_spend:1}]},workspace:{},saved_floor:{floor_minor:120}}; throw Error("unexpected calculation")};
const lifecycle=vm.runInNewContext(source.slice(calcStart,calcEnd)+"\n({loadState,loadCalc})",{state:floorState,api,validSavedFloor:helpers.validSavedFloor,settings:()=>({mode:"expected",horizon:14})});
(async()=>{await lifecycle.loadState(); await lifecycle.loadCalc(); assert(floorState.floorUnverified); assert(floorState.calcError); assert.deepEqual(requests,["/api/state"]); console.log("Actual calculation lifecycle blocks unverifiable legacy cash floor")})().catch(e=>{console.error(e);process.exit(1)});
