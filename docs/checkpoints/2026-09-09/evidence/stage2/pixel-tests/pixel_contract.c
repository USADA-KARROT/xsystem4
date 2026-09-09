/* Real parts hit-test and layout flag parser with deterministic texture pixels.
 * SDL only supplies rectangle arithmetic; no SDL/GL window is created. */
#include <stdio.h>
#include <string.h>
#define PE_SetPartsPixelDecide fixture_set_pixel_decide
#include ACTIVITY_SOURCE
#undef PE_SetPartsPixelDecide
#include INPUT_SOURCE

static struct parts sample;
static uint8_t pixels[8][64 * 4];
static int reads, single_reads, failures, checks;
void fixture_set_pixel_decide(int no, bool enabled) { (void)no; sample.pixel_hittest = enabled; }
void *gfx_get_pixels(Texture *t) {
	reads++;
	if (t->handle == 7) return NULL;
	void *p = malloc((size_t)t->w * t->h * 4);
	memcpy(p, pixels[t->handle], (size_t)t->w * t->h * 4);
	return p;
}
SDL_Color gfx_get_pixel(Texture *t, int x, int y) {
	single_reads++;
	return (SDL_Color){0,0,0,pixels[t->handle][((size_t)y*t->w+x)*4+3]};
}
static void check(bool ok,const char *label) {
	checks++;
	if (!ok) { failures++; fprintf(stderr,"FAIL: %s\n",label); }
}
static void prepare(struct parts *p,int handle) {
	memset(p,0,sizeof(*p));
	p->pixel_hittest=true;
	p->origin_mode=1;
	p->global.scale.x=p->global.scale.y=1;
	p->states[0].type=PARTS_CG;
	p->states[0].common.texture=(Texture){handle,4,4};
	parts_set_dims(p,&p->states[0].common,4,4);
}
static void fill(int handle,unsigned char alpha) {
	for(int i=0;i<64;i++)pixels[handle][i*4+3]=alpha;
}
static void alpha(int handle,int x,int y,unsigned char a) { pixels[handle][(y*4+x)*4+3]=a; }
static bool hit(struct parts *p,int x,int y) { return parts_hittest(p,0,(Point){x,y}); }
static void clear(struct parts *p) {
	for(int i=0;i<PARTS_NR_STATES;i++)parts_clear_hit_mask(&p->states[i].common);
}
static void flags(void) {
	const char *keys[]={SJIS_PIXEL_DECIDE,GBK_PIXEL_DECIDE,GBK_CN_PIXEL_DECIDE};
	struct ex_tree property={.is_leaf=true,.leaf.value={.type=EX_INT}};
	struct ex_tree node={.nr_children=1,.children=&property};
	for(unsigned i=0;i<sizeof(keys)/sizeof(*keys);i++) {
		property.name=cstr_to_string(keys[i]);
		property.leaf.value.i=1;pactex_apply_pixel_decide(&node,1);
		check(sample.pixel_hittest,"layout's exact encoded flag enables pixel decision");
		property.leaf.value.i=0;pactex_apply_pixel_decide(&node,1);
		check(!sample.pixel_hittest,"explicit zero disables pixel decision");
		free_string(property.name);
	}
	property.name=cstr_to_string("not the pixel field");property.leaf.value.i=1;
	sample.pixel_hittest=true;pactex_apply_pixel_decide(&node,1);
	check(!sample.pixel_hittest,"missing flag preserves rectangle default");free_string(property.name);
	property.name=cstr_to_string(GBK_CN_PIXEL_DECIDE);property.leaf.value.type=EX_STRING;
	pactex_apply_pixel_decide(&node,1);check(!sample.pixel_hittest,"wrong field type does not enable alpha");
	free_string(property.name);
}
int main(void) {
	flags();
	struct parts upper,lower,parent;
	prepare(&upper,1);prepare(&lower,2);fill(1,255);fill(2,255);alpha(1,1,1,0);
	upper.pixel_hittest=false;
	check(hit(&upper,1,1),"disabled flag retains rectangle hit on transparent pixel");
	check(reads==0,"disabled flag does not read GPU pixels");
	upper.pixel_hittest=true;
	check(!hit(&upper,1,1),"enabled flag rejects transparent upper image");
	struct parts *target=hit(&upper,1,1)?&upper:hit(&lower,1,1)?&lower:NULL;
	check(target==&lower,"transparent upper button lets opaque lower button win");
	check(hit(&upper,0,1),"opaque upper image remains clickable");
	check(reads==2,"CG alpha read once per state across repeated hits");
	check(!hit(&upper,-1,0)&&!hit(&upper,4,0)&&!hit(&upper,0,4),"image boundaries are half-open");
	int before=reads;hit(&upper,0,0);check(reads==before,"cached repeated CG hit performs no new readback");
	alpha(1,1,1,255);parts_set_dims(&upper,&upper.states[0].common,4,4);
	check(upper.states[0].common.hit_mask==NULL,"CG replacement/dimension update drops cached mask");
	check(hit(&upper,1,1)&&reads==before+1,"same-size texture replacement refreshes alpha");
	clear(&upper);clear(&upper);check(!upper.states[0].common.hit_mask,"cache release is idempotent");
	prepare(&upper,1);fill(1,0);alpha(1,1,2,1);
	memset(&parent,0,sizeof(parent));parent.global.pos=(Point){100,200};upper.parent=&parent;
	upper.local.pos=(Point){10,20};upper.global.pos=(Point){110,220};parts_set_dims(&upper,&upper.states[0].common,4,4);
	check(hit(&upper,111,222),"parent/local translation addresses the opaque texel");
	check(!hit(&upper,112,222),"translated transparent texel rejects");
	clear(&upper);prepare(&upper,1);fill(1,0);alpha(1,1,2,255);
	upper.global.scale.x=upper.global.scale.y=2;
	check(hit(&upper,2,4),"scaled image samples correct opaque pixel outside old bbox");
	check(!hit(&upper,4,4),"scaled transparent pixel rejects");
	upper.global.scale.x=0;check(!hit(&upper,1,1),"zero scale is not hittable");
	upper.global.scale.x=NAN;check(!hit(&upper,1,1),"non-finite transform is not cast to integer");
	clear(&upper);prepare(&upper,1);fill(1,0);alpha(1,1,2,255);
	upper.sprite_deform=1;check(hit(&upper,2,2)&&!hit(&upper,1,2),"horizontal flip samples mirrored alpha");
	upper.sprite_deform=2;check(hit(&upper,1,1)&&!hit(&upper,1,2),"vertical flip samples mirrored alpha");
	upper.sprite_deform=0;upper.local.rotation.z=90;
	check(hit(&upper,-2,1),"rotation follows rendered image transform");
	clear(&upper);prepare(&upper,1);fill(1,0);alpha(1,2,1,255);
	parts_set_surface_area(&upper,&upper.states[0].common,1,1,2,2);
	check(hit(&upper,1,0),"clipped image uses full origin rather than stretching its alpha");
	check(!hit(&upper,0,0),"transparent texel inside crop is rejected");
	check(!hit(&upper,2,0),"texel outside clipped surface is rejected");
	clear(&upper);prepare(&upper,1);fill(1,0);alpha(1,0,0,255);
	upper.states[0].type=PARTS_HGAUGE;
	check(hit(&upper,0,0),"dynamic texture starts with live opaque alpha");
	alpha(1,0,0,0);check(!hit(&upper,0,0),"dynamic same-handle texture change is immediately visible");
	check(single_reads==2&&!upper.states[0].common.hit_mask,"dynamic parts never cache stale alpha");
	prepare(&upper,0);check(hit(&upper,1,1),"no texture retains rectangle fallback");
	prepare(&upper,7);check(hit(&upper,1,1),"unavailable pixel read retains rectangle fallback");
	clear(&upper);clear(&lower);
	printf("%d checks, %d failures; no SDL or GL initialization\n",checks,failures);
	return failures?1:0;
}
