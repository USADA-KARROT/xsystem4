from pathlib import Path
import argparse,hashlib,json,shlex,subprocess
here=Path(__file__).resolve().parent;root=here.parents[1];source=root/'work/stage2/source'
p=argparse.ArgumentParser();p.add_argument('variant',choices=['optimized','asan']);p.add_argument('--xassign-transfer',action='store_true');a=p.parse_args();suffix=a.variant+('-transfer' if a.xassign_transfer else '')
build=root/('work/stage2/'+a.variant+'-build')
text=(source/'src/vm.c').read_text()
if a.xassign_transfer:
    start=text.index('\tcase X_ASSIGN: {');end=text.index('\tcase X_DUP:',start)
    block=text[start:end];assert block.count('if (xa_page) {')==1
    block=block.replace('if (xa_page) {','if (ain->version < 14 && xa_page) {')
    text=text[:start]+block+text[end:]
start=text.index('static int _function_call(');brace=text.index('{',start)
text=text[:brace+1]+'\n\tprobe_entry(fno);'+text[brace+1:]
start=text.index('static void function_return(void)\n{');anchor='\tint base_sp = call_stack[call_stack_ptr-1].base_sp;';pos=text.index(anchor,start)
text=text[:pos]+'\tprobe_return(fno, page_slot);\n'+text[pos:]
anchor='\t\topcode = get_opcode(instr_ptr);';assert text.count(anchor)==1
text=text.replace(anchor,anchor+'\n\t\tprobe_step(opcode);')
(here/'instrumented-vm.inc').write_text(text)
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
vmobj=here/('probe-'+suffix+'.o');sysobj=here/('system-'+suffix+'.o')
compiled=[compile_as('vm.c',here/'runtime_probe.c',vmobj,['-DFFI_SOURCE="'+str(source/'src/ffi.c')+'"']),compile_as('system4.c',source/'src/system4.c',sysobj,['-Dmain=xsystem4_original_main'])]
link=shlex.split(subprocess.check_output(['ninja','-t','commands','src/xsystem4'],cwd=build,text=True).splitlines()[-1])
link.remove('src/xsystem4.p/ffi.c.o')
binary=here/('runtime-probe-'+suffix)
for i,arg in enumerate(link):
    if arg=='src/xsystem4':link[i]=str(binary)
    elif arg=='src/xsystem4.p/vm.c.o':link[i]=str(vmobj)
    elif arg=='src/xsystem4.p/system4.c.o':link[i]=str(sysobj)
run=subprocess.run(link,cwd=build,capture_output=True,text=True)
(here/('link-'+suffix+'.log')).write_text(run.stdout+run.stderr)
if run.returncode:raise SystemExit(run.stderr)
result={'variant':a.variant,'counterfactual_xassign_v14_transfer':a.xassign_transfer,'vm_sha256':hashlib.sha256((source/'src/vm.c').read_bytes()).hexdigest(),'probe_sha256':hashlib.sha256((here/'runtime_probe.c').read_bytes()).hexdigest(),'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'compile_commands':compiled,'link_command':link,'scope':'Full production VM with entry/return/step counters only; original timer interception flags disabled in harness; actual engine objects linked; original system4 main renamed, never run.'}
if a.xassign_transfer:result['scope']+=' Counterfactual only: X_ASSIGN v14 is raw transfer, no automatic retain/release.'
result['source_sha256']={str(p.relative_to(source)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [source/'src/vm.c',source/'src/page.c',source/'src/heap.c',source/'src/ffi.c',source/'src/hll/Array.c',source/'include/vm/page.h']}
(here/('build-'+suffix+'.json')).write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:result[k] for k in ['variant','vm_sha256','binary_sha256']}))
