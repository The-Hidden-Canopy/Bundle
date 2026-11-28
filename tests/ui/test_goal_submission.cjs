const fs=require("fs"),vm=require("vm"),assert=require("assert"),path=require("path");
const src=fs.readFileSync(path.join(__dirname,"../../ui/bundle_ui/static/app.js"),"utf8");
function block(a,b){const start=src.indexOf(a),end=src.indexOf(b,start);assert(start>=0&&end>start);return src.slice(start,end)}
const parser=block('  const toMinor = (text, currency = "USD") => {','  const fmtDate = ');
const submitSource=block('  document.addEventListener("submit", async (event) => {','\n  });')+"\n  });";
let submit, calls=[], out={innerHTML:""};
const ctx={document:{addEventListener:(_,f)=>submit=f,getElementById:()=>out},act:async(_,fn)=>fn(),
 api:async(path,options)=>{calls.push([path,options]);return {created:true}},refresh:async()=>{},render:()=>{},errorNotice:e=>String(e),console};
vm.runInNewContext("const EXPONENT={JPY:0,KWD:3};"+parser+"\n"+submitSource,ctx);
const form={id:"goal-form",dataset:{requestId:"0123456789abcdef0123456789abcdef",workspace:"synthetic"},
 querySelector:()=>({}), currency:{value:"KWD"}, amount:{value:"123.456"},goal_name:{value:"Synthetic"},
 date:{value:""},protected_goal:{value:"no"}};
async function apply(){await submit({target:form,preventDefault(){}})}
(async()=>{
 for(const value of ["0","-1","1.0000","abc","1.2.3"]){form.amount.value=value;calls=[];await apply();assert.equal(calls.length,0);assert(out.innerHTML.includes('role="alert"'))}
 form.amount.value="123.456";await apply();assert.equal(calls.length,1);
 const [endpoint,request]=calls[0];assert.equal(endpoint,"/api/goals/create");assert.equal(request.method,"POST");
 assert.equal(request.body.target_minor,123456);assert.equal(request.body.currency,"KWD");assert.equal(request.body.target_date,null);
 assert.equal(request.body.protected,false);assert.equal(request.body.request_id,form.dataset.requestId);
 assert.equal(request.body.workspace_database,"synthetic");assert(out.innerHTML.includes("No funds were moved"));
 console.log("Synthetic actual goal submission: five invalid inputs refused, exact KWD and workspace/retry identity sent");
})().catch(e=>{console.error(e);process.exit(1)});
