// Offline DOM-contract check, not a browser rendering/visual test.
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '../..');
const html = fs.readFileSync(path.join(root, 'outputs/原生研究-API地圖-2026-09-20.html'), 'utf8');
const dataset = html.match(/<script id="dataset" type="application\/json">([\s\S]*?)<\/script>/)[1];
const source = html.match(/<script>([\s\S]*?)<\/script>/)[1];
class Element {
  constructor() { this.value = ''; this.checked = false; this.textContent = ''; this.children = []; this.listeners = {}; }
  append(child) { this.children.push(child); }
  replaceChildren(fragment) { this.children = fragment.children; }
  addEventListener(name, callback) { this.listeners[name] = callback; }
}
const elements = Object.fromEntries(['dataset','search','library','mapped','overload','count','rows','identity'].map(id => [id, new Element()]));
elements.dataset.textContent = dataset;
const document = { getElementById: id => elements[id], createElement: () => new Element(), createDocumentFragment: () => new Element() };
vm.runInNewContext(source, { document });
const cases = [];
function check(label, expected) {
  assert.equal(elements.rows.children.length, expected, label);
  assert.match(elements.count.textContent, new RegExp(expected.toLocaleString()));
  cases.push({ label, rows: expected, passed: true });
}
check('All declarations', 1701);
elements.library.value = 'Array'; elements.library.listeners.input(); check('Array library', 84);
elements.search.value = 'IsExist'; elements.search.listeners.input(); check('Array IsExist overloads', 2);
assert(elements.rows.children.every(row => row.children[1].children[0].textContent.includes('IsExist(')));
elements.search.value = ''; elements.library.value = ''; elements.mapped.checked = true; elements.mapped.listeners.input(); check('EXE mapped', 952);
elements.mapped.checked = false; elements.overload.checked = true; elements.overload.listeners.input(); check('Same-name overloads', 97);
elements.overload.checked = false; elements.search.value = '0x644572'; elements.search.listeners.input(); check('VA lookup', 1);
elements.search.value = 'this-declaration-does-not-exist'; elements.search.listeners.input(); check('No match', 0);
const data = JSON.parse(dataset);
assert.equal(new Set(data.rows.map(r => r.library)).size, 36);
assert.equal(data.rows.filter(r => r.studied).length, 8);
assert.equal(data.rows.reduce((n,r) => n+r.static_sites,0), 11590);
const result = { passed: true, scope: 'Offline DOM contract and embedded data validation only. No browser connection was available; visual rendering not checked.', libraries: 36, partially_studied: 8, static_call_sites: 11590, cases };
fs.writeFileSync(path.join(__dirname, 'probes/report-api-check.json'), JSON.stringify(result, null, 2) + '\n');
console.log(JSON.stringify(result, null, 2));
