const fs=require('fs'),vm=require('vm'),assert=require('assert');
const path=require('path'); const src=fs.readFileSync(process.argv[2] || path.join(__dirname,'../../ui/bundle_ui/static/app.js'), 'utf8');
function block(start,end){const a=src.indexOf(start),b=src.indexOf(end,a);assert(a>=0&&b>a);return src.slice(a,b)}
const parser=block('  const toMinor = (text, currency = "USD") => {','  const fmtDate = ');
const marker='  document.addEventListener("submit", async (event) => {';
const a=src.indexOf(marker), b=src.indexOf('\n  });',a);assert(a>=0&&b>a);const code=src.slice(a,b)+'\n  });';
let submit,calls=[],out={innerHTML:''};
const context={document:{addEventListener:(ev,cb)=>{assert.equal(ev,'submit');submit=cb},getElementById:()=>out},currencyOf:()=> 'USD',settings:()=>({mode:'expected',horizon:14}),state:{savedFloor:null},act:async(_,fn)=>fn(),api:async(path,options)=>{calls.push([path,options]);return {what_if:{safe_to_spend_before_minor:0,safe_to_spend_after_minor:0,minimum_balance_before_minor:0,minimum_balance_after_minor:0,warnings:[]}}},refresh:async()=>{},render:()=>{},money:()=>'',errorNotice:()=>'',console};
vm.runInNewContext('const EXPONENT={JPY:0,KWD:3};'+parser+'\n'+code,context);
async function test(id,field,value,expected){calls=[];out.innerHTML='';const form={id,querySelector:()=>({}),[field]:{value},date:{value:'2026-11-26'}};await submit({target:form,preventDefault(){}});assert.strictEqual(calls.length,expected,`${id} ${value}`);if(!expected)assert(out.innerHTML.includes('role="alert"'));}
(async()=>{for(const id of ['floor-form','whatif-form']){const field=id==='floor-form'?'floor':'amount';for(const v of ['-1','1.999','12x','1.2.3','9007199254740999999'])await test(id,field,v,0);await test(id,field,'500.00',1)}console.log('12 actual form-handler synthetic submission cases passed; invalid values make no API call')})().catch(e=>{console.error(e);process.exit(1)});
