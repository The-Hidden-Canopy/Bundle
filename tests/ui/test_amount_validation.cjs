const fs = require('fs'); const vm = require('vm'); const assert = require('assert');
const path = require('path'); const source = fs.readFileSync(process.argv[2] || path.join(__dirname, '../../ui/bundle_ui/static/app.js'), 'utf8');
const start = source.indexOf('  const toMinor = (text, currency = "USD") => {');
const end = source.indexOf('  const fmtDate = ',start);
assert(start>=0 && end>start);
const toMinor = vm.runInNewContext('const EXPONENT = {JPY:0,KWD:3};\n'+source.slice(start,end)+'\ntoMinor');
const cases=[['0','USD',0],[' 500.00 ','USD',50000],['1,234.56','USD',123456],['0.01','USD',1],['.50','USD',50],['123','JPY',123],['0.001','KWD',1],['-100','USD',null],['+1','USD',null],['1.999','USD',null],['1.2.3','USD',null],['abc100','USD',null],['1,23','USD',null],['12,34.00','USD',null],['.','USD',null],['','USD',null],['1.0','JPY',null],['1.0000','KWD',null],['999999999999999999999999','USD',null],['1e3','USD',null],['$1','USD',null]];
for (const [v,c,expected] of cases) assert.strictEqual(toMinor(v,c),expected,JSON.stringify([v,c]));
assert(source.includes('if (minor === null)')); assert(source.includes('if (!minor)')); console.log(`${cases.length} currency input cases passed; floor and what-if reject null before API`);
