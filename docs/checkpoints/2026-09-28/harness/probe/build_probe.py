from pathlib import Path
import argparse,hashlib,json,os,shlex,subprocess
# XS4_SRC / XS4_ASAN_BUILD / XS4_OPTIMIZED_BUILD override the original fixed layout (see ../README.md).
here=Path(__file__).resolve().parent;root=here.parents[2]
source=Path(os.environ.get('XS4_SRC') or root/'work/stage2/source')
p=argparse.ArgumentParser();p.add_argument('variant',choices=['optimized','asan']);a=p.parse_args()
build=Path(os.environ.get('XS4_'+a.variant.upper()+'_BUILD') or root/('work/stage2/'+a.variant+'-build'))
text=(source/'src/vm.c').read_text()
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
vmobj=here/('probe-'+a.variant+'.o');sysobj=here/('system-'+a.variant+'.o')
compiled=[compile_as('vm.c',here/'runtime_probe.c',vmobj,['-DFFI_SOURCE="'+str(source/'src/ffi.c')+'"','-I'+str(source/'src/parts')]),compile_as('system4.c',source/'src/system4.c',sysobj,['-Dmain=xsystem4_original_main'])]
# Probe-only entry points expose existing static initialization/loader functions.
# Never patch production behavior or initialize GL for synthetic empty text.
parts_shadow = here/'probe-parts.c'
parts_shadow.write_text((source/'src/parts/parts.c').read_text()+"\nvoid probe_init_parts_table(void) { parts_table = ht_create(1024); ctrl_stack_init(); }\n")
activity_shadow = here/'probe-activity.c'
activity_shadow.write_text((source/'src/hll/pe_v14_activity.c').read_text()+"\nbool probe_load_activity_tree(struct string *name, struct ex *ex) { if (!PartsEngine_CreateActivity(name)) return false; int i = find_activity(name); return i >= 0 && pactex_load(&activities[i], ex); }\n")
partsobj=here/('parts-'+a.variant+'.o'); activityobj=here/('activity-'+a.variant+'.o')
compiled += [compile_as('parts/parts.c', parts_shadow, partsobj, ['-I'+str(source/'src/parts')]), compile_as('hll/pe_v14_activity.c', activity_shadow, activityobj, ['-I'+str(source/'src/hll')])]
# The GB18030 text walkers are static; expose them to gbk_chars_fixture.inc the same way.
text_shadow = here/'probe-text.c'
text_shadow.write_text((source/'src/text.c').read_text()+"\nint probe_gb18030_skip_char_bytes(const char *s) { return gb18030_skip_char_bytes(s); }\n")
ptext_shadow = here/'probe-parts-text.c'
ptext_shadow.write_text((source/'src/parts/text.c').read_text()+"\nint probe_extract_multibyte_char(const char *src, char *dst) { return extract_multibyte_char(src, dst); }\n")
textobj=here/('text-'+a.variant+'.o'); ptextobj=here/('parts-text-'+a.variant+'.o')
compiled += [compile_as('src/text.c', text_shadow, textobj), compile_as('parts/text.c', ptext_shadow, ptextobj, ['-I'+str(source/'src/parts')])]
# text_metrics_fixture.inc asks whether a missing glyph falls back to the other font (static
# ft_font_fallback_face, added in 05d2441; older revisions have no fallback).
ft_text=(source/'src/font_freetype.c').read_text()
if 'ft_font_fallback_face' in ft_text:
    ft_text+="\nbool probe_ft_font_fallback(struct font *f, uint32_t code, unsigned size) { struct font_ft *font = (struct font_ft*)f; return ft_font_fallback_face(font, code, size) != font->font; }\n"
else:
    ft_text+="\nbool probe_ft_font_fallback(struct font *f, uint32_t code, unsigned size) { (void)f; (void)code; (void)size; return false; }\n"
ft_shadow=here/'probe-font-freetype.c'; ft_shadow.write_text(ft_text)
ftobj=here/('font-freetype-'+a.variant+'.o')
compiled += [compile_as('src/font_freetype.c', ft_shadow, ftobj)]
link=shlex.split(subprocess.check_output(['ninja','-t','commands','src/xsystem4'],cwd=build,text=True).splitlines()[-1])
link.remove('src/xsystem4.p/ffi.c.o')
binary=here/('runtime-probe-'+a.variant)
for i,arg in enumerate(link):
    if arg=='src/xsystem4':link[i]=str(binary)
    elif arg=='src/xsystem4.p/vm.c.o':link[i]=str(vmobj)
    elif arg=='src/xsystem4.p/system4.c.o':link[i]=str(sysobj)
    elif arg=='src/xsystem4.p/parts_parts.c.o':link[i]=str(partsobj)
    elif arg=='src/xsystem4.p/hll_pe_v14_activity.c.o':link[i]=str(activityobj)
    elif arg=='src/xsystem4.p/text.c.o':link[i]=str(textobj)
    elif arg=='src/xsystem4.p/parts_text.c.o':link[i]=str(ptextobj)
    elif arg=='src/xsystem4.p/font_freetype.c.o':link[i]=str(ftobj)
run=subprocess.run(link,cwd=build,capture_output=True,text=True)
(here/('link-'+a.variant+'.log')).write_text(run.stdout+run.stderr)
if run.returncode:raise SystemExit(run.stderr)
result={'variant':a.variant,'vm_sha256':hashlib.sha256((source/'src/vm.c').read_bytes()).hexdigest(),'probe_sha256':hashlib.sha256((here/'runtime_probe.c').read_bytes()).hexdigest(),'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'compile_commands':compiled,'link_command':link,'scope':'Real-AIN VM/HLL harness; no GUI. Production parts/activity source copies append probe-only static-entry wrappers; no behavior replacement. Other engine objects link from the current build; original main renamed.'}
result['source_sha256']={str(p.relative_to(source)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [source/'src/vm.c',source/'src/page.c',source/'src/heap.c',source/'src/ffi.c',source/'src/hll/Array.c',source/'include/vm/page.h',source/'src/parts/parts.c',source/'src/hll/pe_v14_activity.c',source/'src/parts/parts_internal.h',source/'src/text.c',source/'src/parts/text.c',source/'src/font_freetype.c']}
result['fixture_sha256']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in here.iterdir() if p.is_file() and p.suffix in ['.c','.inc','.py'] and p.name!='instrumented-vm.inc'}
result['source_sha256']['include/vm.h']=hashlib.sha256((source/'include/vm.h').read_bytes()).hexdigest()
(here/('build-'+a.variant+'.json')).write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:result[k] for k in ['variant','vm_sha256','binary_sha256']}))
