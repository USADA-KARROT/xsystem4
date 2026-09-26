from pathlib import Path
import argparse,hashlib,json,shlex,subprocess
here=Path(__file__).resolve().parent;root=here.parents[1];source=root/'work/stage2/source'
p=argparse.ArgumentParser();p.add_argument('variant',choices=['optimized','asan']);p.add_argument('--transfer',action='store_true');p.add_argument('--no-wrap-retain',action='store_true');a=p.parse_args()
assert not a.no_wrap_retain or a.transfer
tag=a.variant+('-transfer' if a.transfer else '')+('-no-wrap-retain' if a.no_wrap_retain else '')
build=root/('work/stage2/'+a.variant+'-build')
text=(here/'baseline-vm.c').read_text()
if a.transfer:
    start=text.index('case X_ASSIGN: {');end=text.index('case X_DUP:',start)
    block=text[start:end]
    anchor='\t\t\t\tif (xa_page) {'
    assert block.count(anchor)==1
    block=block.replace(anchor,'\t\t\t\tif (xa_page && ain->version < 14) {')
    text=text[:start]+block+text[end:]

start=text.index('static int _function_call(');brace=text.index('{',start)
text=text[:brace+1]+'\n\tprobe_entry(fno);'+text[brace+1:]
start=text.index('static void function_return(void)\n{');anchor='\tint base_sp = call_stack[call_stack_ptr-1].base_sp;';pos=text.index(anchor,start)
text=text[:pos]+'\tprobe_return(fno, page_slot);\n'+text[pos:]
anchor='\t\topcode = get_opcode(instr_ptr);';assert text.count(anchor)==1
text=text.replace(anchor,anchor+'\n\t\tprobe_step(opcode);')
(here/'instrumented-vm.inc').write_text(text)
heap_text=(here/'baseline-heap.c').read_text()
heap_text='extern void review_heap_event(const char *action, int slot);\n'+heap_text
for signature, action in [('void heap_ref(int32_t slot)', 'retain'), ('void heap_unref(int slot)', 'release')]:
    where=heap_text.index(signature);brace=heap_text.index('{',where)
    heap_text=heap_text[:brace+1]+'\n\treview_heap_event("'+action+'",slot);'+heap_text[brace+1:]
(here/'instrumented-heap.c').write_text(heap_text)
commands=json.loads((build/'compile_commands.json').read_text())
def compile_as(original,newfile,out,extra=[]):
    entry=next(c for c in commands if c['file'].endswith('/'+original))
    args=shlex.split(entry['command']);clean=[];i=0
    while i<len(args):
        if args[i] in ['-o','-MF','-MQ']:i+=2;continue
        if args[i]=='-MD':i+=1;continue
        if args[i]==entry['file']:clean.append(str(newfile))
        else:clean.append(args[i])
        i+=1
    clean+=extra+['-o',str(out)]
    run=subprocess.run(clean,cwd=build,capture_output=True,text=True)
    (here/(out.name+'.log')).write_text(run.stdout+run.stderr)
    if run.returncode:raise SystemExit(run.stderr)
    return clean
ffi_source=here/'baseline-ffi.c'
if a.no_wrap_retain:
    ffi_text=ffi_source.read_text()
    anchor='heap_ref(hll_self_slot); // owning wrap result, released by bytecode DELETE'
    assert ffi_text.count(anchor)==1
    ffi_text=ffi_text.replace(anchor,'/* PRIVATE negative control: removed owning wrap retain. */')
    ffi_source=here/'negative-ffi.c';ffi_source.write_text(ffi_text)
vmobj=here/('probe-'+tag+'.o');sysobj=here/('system-'+tag+'.o')
compiled=[compile_as('vm.c',here/'runtime_probe.c',vmobj,['-DFFI_SOURCE="'+str(ffi_source)+'"']),compile_as('system4.c',source/'src/system4.c',sysobj,['-Dmain=xsystem4_original_main'])]
heapobj=here/('heap-'+tag+'.o')
compiled.append(compile_as('heap.c',here/'instrumented-heap.c',heapobj))
link=shlex.split(subprocess.check_output(['ninja','-t','commands','src/xsystem4'],cwd=build,text=True).splitlines()[-1])
link.remove('src/xsystem4.p/ffi.c.o')
binary=here/('runtime-probe-'+tag)
for i,arg in enumerate(link):
    if arg=='src/xsystem4':link[i]=str(binary)
    elif arg=='src/xsystem4.p/vm.c.o':link[i]=str(vmobj)
    elif arg=='src/xsystem4.p/system4.c.o':link[i]=str(sysobj)
    elif arg=='src/xsystem4.p/heap.c.o':link[i]=str(heapobj)
run=subprocess.run(link,cwd=build,capture_output=True,text=True)
(here/('link-'+tag+'.log')).write_text(run.stdout+run.stderr)
if run.returncode:raise SystemExit(run.stderr)
result={'variant':tag,'private_transfer_candidate':a.transfer,'negative_no_wrap_retain':a.no_wrap_retain,'instrumented_vm_sha256':hashlib.sha256((here/'instrumented-vm.inc').read_bytes()).hexdigest(),'vm_sha256':hashlib.sha256((here/'baseline-vm.c').read_bytes()).hexdigest(),'probe_sha256':hashlib.sha256((here/'runtime_probe.c').read_bytes()).hexdigest(),'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'compile_commands':compiled,'link_command':link,'scope':'Production VM with entry/return/step/ref diagnostics; when private_transfer_candidate=true, v14 X_ASSIGN retain/release disabled only in this generated copy; original timer interception flags disabled in harness; actual engine objects linked; original system4 main renamed, never run.'}
result['pinned_source_sha256']={f:hashlib.sha256((here/('baseline-'+f)).read_bytes()).hexdigest() for f in ['vm.c','heap.c','ffi.c']}
result['source_sha256']={str(p.relative_to(source)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [source/'src/vm.c',source/'src/page.c',source/'src/heap.c',source/'src/ffi.c',source/'src/hll/Array.c',source/'include/vm/page.h']}
(here/('build-'+tag+'.json')).write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:result[k] for k in ['variant','vm_sha256','binary_sha256']}))
