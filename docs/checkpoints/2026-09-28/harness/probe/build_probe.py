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
if not any(c['file'].endswith('/movie_ffmpeg.c') for c in commands):
    raise SystemExit('movie-audio-only requires the real FFmpeg movie backend')
from generate_movie_fixtures import generate as generate_movie_fixtures
movie_fixture_dir=generate_movie_fixtures(here)
# apeg-video: synthetic APEG movies, their sound is the movie fixtures' Vorbis.
from generate_apeg_fixtures import generate as generate_apeg_fixtures
apeg_fixture_dir=generate_apeg_fixtures(here,movie_fixture_dir/'long-stereo.ogg')
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
compiled=[compile_as('vm.c',here/'runtime_probe.c',vmobj,['-DFFI_SOURCE="'+str(source/'src/ffi.c')+'"','-I'+str(source/'src/parts'),'-DXS4_MOVIE_FIXTURE_DIR='+json.dumps(str(movie_fixture_dir)),'-DXS4_APEG_FIXTURE_DIR='+json.dumps(str(apeg_fixture_dir))]),compile_as('system4.c',source/'src/system4.c',sysobj,['-Dmain=xsystem4_original_main'])]
# Probe-only entry points expose existing static initialization/loader functions.
# Never patch production behavior or initialize GL for synthetic empty text.
parts_shadow = here/'probe-parts.c'
parts_shadow.write_text((source/'src/parts/parts.c').read_text()+"\n/* The v14 controller stack makes a root parts per layer (layer-root), which the parts list keeps; a table made anew while they are there would leave them unreachable, and parts_release_all would spin on them. */ void probe_init_parts_table(void) { if (parts_table) parts_release_all(); parts_table = ht_create(1024); ctrl_stack_init(); }\n")
activity_shadow = here/'probe-activity.c'
activity_shadow.write_text((source/'src/hll/pe_v14_activity.c').read_text()+"\nbool probe_load_activity_tree(struct string *name, struct ex *ex) { if (!PartsEngine_CreateActivity(name)) return false; int i = find_activity(name); return i >= 0 && pactex_load(&activities[i], ex); }\n/* The number the next activity component gets; taken as a pactex load takes it when `take` is set (system-reset). */ int probe_activity_parts_no(bool take) { return take ? alloc_activity_parts_no() : next_activity_parts_no; }\n")
partsobj=here/('parts-'+a.variant+'.o'); activityobj=here/('activity-'+a.variant+'.o')
compiled += [compile_as('parts/parts.c', parts_shadow, partsobj, ['-I'+str(source/'src/parts')]), compile_as('hll/pe_v14_activity.c', activity_shadow, activityobj, ['-I'+str(source/'src/hll')])]
# The GB18030 text walkers are static; expose them to gbk_chars_fixture.inc the same way.
text_shadow = here/'probe-text.c'
text_shadow.write_text((source/'src/text.c').read_text()+"\nint probe_gb18030_skip_char_bytes(const char *s) { return gb18030_skip_char_bytes(s); }\n")
ptext_shadow = here/'probe-parts-text.c'
ptext_shadow.write_text((source/'src/parts/text.c').read_text()+"\nint probe_extract_multibyte_char(const char *src, char *dst) { return extract_multibyte_char(src, dst); }\n")
# base_ui_fixture.inc installs a synthetic EX file into MainEXFile's static state.
mainex_shadow = here/'probe-mainexfile.c'
mainex_shadow.write_text((source/'src/hll/MainEXFile.c').read_text()+"\nvoid probe_mainex_set(struct ex *e) { ex = e; memset(resolve_cache, 0, sizeof(resolve_cache)); }\n")
mainexobj=here/('mainexfile-'+a.variant+'.o')
compiled += [compile_as('hll/MainEXFile.c', mainex_shadow, mainexobj, ['-I'+str(source/'src/hll')])]
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
# construction_fixture.inc builds parts textures in a windowless GL context. gl_initialize is static:
# it loads the default shader, creates the quad buffers and reads the maximum texture size (0 until
# then, which makes every new texture 0x0).
video_shadow=here/'probe-video.c'
# save_thumbnail_fixture.inc draws the scene: the main surface as gfx_set_window_logical_size makes it,
# without the window that function asks for its drawable size. The engine's surface has no alpha; this
# one has, like the textures the other fixtures draw into (which draws the windowless context drops
# is in save_thumbnail_fixture.inc).
video_shadow.write_text((source/'src/video.c').read_text()+"\nvoid probe_gl_initialize(void) { gl_initialize(); }\n"
    "void probe_main_surface_init(int w, int h) { sdl.w = sdl.viewport.w = w; sdl.h = sdl.viewport.h = h; sdl.viewport.x = sdl.viewport.y = 0;"
    " world_view_transform[0][0] = 2.0 / w; world_view_transform[1][1] = 2.0 / h; gfx_delete_texture(&main_surface);"
    " gfx_init_texture_blank(&main_surface, w, h); glGenFramebuffers(1, &main_surface_fb); glBindFramebuffer(GL_FRAMEBUFFER, main_surface_fb);"
    " glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, main_surface.handle, 0); glViewport(0, 0, w, h); }\n")
videoobj=here/('video-'+a.variant+'.o')
compiled += [compile_as('src/video.c', video_shadow, videoobj)]
# construction_cg_fixture.inc needs CGs and the probe has no archives: cg_assets_shadow.inc serves
# files made in memory through the asset manager's own (static) table.
asset_shadow=here/'probe-asset-manager.c'
asset_shadow.write_text((source/'src/asset_manager.c').read_text()+'\n'+(here/'cg_assets_shadow.inc').read_text())
assetobj=here/('asset-manager-'+a.variant+'.o')
compiled += [compile_as('src/asset_manager.c', asset_shadow, assetobj)]
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
    elif arg=='src/xsystem4.p/hll_MainEXFile.c.o':link[i]=str(mainexobj)
    elif arg=='src/xsystem4.p/video.c.o':link[i]=str(videoobj)
    elif arg=='src/xsystem4.p/asset_manager.c.o':link[i]=str(assetobj)
run=subprocess.run(link,cwd=build,capture_output=True,text=True)
(here/('link-'+a.variant+'.log')).write_text(run.stdout+run.stderr)
if run.returncode:raise SystemExit(run.stderr)
result={'variant':a.variant,'vm_sha256':hashlib.sha256((source/'src/vm.c').read_bytes()).hexdigest(),'probe_sha256':hashlib.sha256((here/'runtime_probe.c').read_bytes()).hexdigest(),'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'compile_commands':compiled,'link_command':link,'scope':'Real-AIN VM/HLL harness; no GUI. Production parts/activity source copies append probe-only static-entry wrappers; no behavior replacement. Other engine objects link from the current build; original main renamed.'}
result['source_sha256']={str(p.relative_to(source)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [source/'src/vm.c',source/'src/page.c',source/'src/heap.c',source/'src/ffi.c',source/'src/hll/Array.c',source/'include/vm/page.h',source/'src/parts/parts.c',source/'src/hll/pe_v14_activity.c',source/'src/parts/parts_internal.h',source/'src/text.c',source/'src/parts/text.c',source/'src/font_freetype.c',source/'src/hll/MainEXFile.c',source/'src/video.c']}
result['source_sha256']['src/movie_ffmpeg.c']=hashlib.sha256((source/'src/movie_ffmpeg.c').read_bytes()).hexdigest()
result['movie_fixture_manifest']={'path':str(movie_fixture_dir/'manifest.json'),'sha256':hashlib.sha256((movie_fixture_dir/'manifest.json').read_bytes()).hexdigest()}
result['apeg_fixture_manifest']={'path':str(apeg_fixture_dir/'manifest.json'),'sha256':hashlib.sha256((apeg_fixture_dir/'manifest.json').read_bytes()).hexdigest()}
for f in ['src/apeg.c','include/apeg.h']:
    if (source/f).exists():result['source_sha256'][f]=hashlib.sha256((source/f).read_bytes()).hexdigest()
result['fixture_sha256']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in here.iterdir() if p.is_file() and p.suffix in ['.c','.inc','.py'] and p.name!='instrumented-vm.inc'}
result['source_sha256']['include/vm.h']=hashlib.sha256((source/'include/vm.h').read_bytes()).hexdigest()
(here/('build-'+a.variant+'.json')).write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:result[k] for k in ['variant','vm_sha256','binary_sha256']}))
