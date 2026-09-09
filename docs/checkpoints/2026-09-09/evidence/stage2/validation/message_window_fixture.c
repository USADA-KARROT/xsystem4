/* Isolated production sidecar tests; text rasterization and parts lookup are
 * fixtures. Real libsys4 strings run under ASan/UBSan. No SDL window or VM. */
#include <assert.h>
#include <stdio.h>
#include "../source/src/parts/message_window.c"

static struct parts fixture;
bool parts_message_window_show = true;
static struct string *captured_plain;
struct parts *parts_get(int number) { assert(number == 7); return &fixture; }
struct parts *parts_try_get(int number) { return number == 7 ? &fixture : NULL; }
void parts_state_reset(struct parts_state *state, enum parts_type type) {
    memset(state, 0, sizeof(*state));
    state->type = type;
    state->text.ts.size = 16;
}
void parts_text_free(struct parts_text *text) { text->nr_lines = 0; }
void parts_text_append(struct parts *parts, struct parts_text *text, struct string *str) {
    assert(parts == &fixture);
    if (captured_plain) free_string(captured_plain);
    captured_plain = string_dup(str);
    text->nr_lines = str->size ? 1 : 0;
    text->common.w = str->size;
    text->common.h = str->size ? 16 : 0;
    text->common.origin_offset = (Point){-999, -999}; /* Must not affect rendering. */
}
void parts_dirty(struct parts *parts) { assert(parts == &fixture); }
bool PE_SetPartsCG(int number, struct string *name, int deform, int state) {
    assert(number == 7 && deform == 0 && state == 1);
    struct parts_state *s = &fixture.states[0];
    if (s->type == PARTS_CG && s->cg.name) free_string(s->cg.name);
    s->type = PARTS_CG;
    s->cg.name = string_dup(name);
    s->common.origin_offset = (Point){-640,-720};
    return true;
}
static void assert_text(struct string *actual, const char *expected) {
    assert(actual && actual->size == (int)strlen(expected));
    assert(!memcmp(actual->text, expected, actual->size));
    free_string(actual);
}
int main(void) {
    unsigned empty_before = EMPTY_STRING.ref;
    fixture.no = 7;
    fixture.global.show = true;
    fixture.global.pos = (Point){640,720};
    fixture.states[0].type = PARTS_CG;
    fixture.states[0].cg.name = make_string("activity-background", 19);
    fixture.states[0].common.origin_offset = (Point){-640,-720};
    struct string *initial_name = fixture.states[0].cg.name;
    assert_text(PE_GetMessageWindowCGName(7), "activity-background");
    assert(!fixture.message && fixture.states[0].cg.name == initial_name);
    assert_text(PE_GetMessageWindowCGName(999), "");
    assert_text(PE_GetMessageWindowText(999), "");

    const char *raw = "${time 0}\xc4\xe3\xba\xc3${/time}${font r=255}!${/font}\x81\x30\x81\x30";
    const char *plain = "\xc4\xe3\xba\xc3!\x81\x30\x81\x30";
    struct string *input = make_string(raw, strlen(raw));
    PE_SetMessageWindowText(7, input, 0, NULL, 0, 0);
    free_string(input);
    assert_text(string_ref(captured_plain), plain);
    assert_text(PE_GetMessageWindowText(7), raw);
    assert(fixture.states[0].type == PARTS_CG && fixture.states[0].cg.name == initial_name);
    assert_text(PE_GetMessageWindowCGName(7), "activity-background");
    assert(fixture.message->text.nr_lines == 1);

    /* Match the AIN composer: get original text, append, set it again. */
    struct string *composed = PE_GetMessageWindowText(7);
    string_append_cstr(&composed, " next", 5);
    PE_SetMessageWindowText(7, composed, 0, NULL, 0, 0);
    free_string(composed);
    assert_text(string_ref(captured_plain), "\xc4\xe3\xba\xc3!\x81\x30\x81\x30 next");

    struct string *bg = make_string("new-background", 14);
    PE_SetMessageWindowCGName(7, bg);
    free_string(bg);
    assert_text(PE_GetMessageWindowCGName(7), "new-background");
    assert(fixture.message->text.nr_lines == 1);
    PE_SetMessageWindowTextArea(7, 360, 600, 600, 118);
    PE_SetMessageWindowTextOriginPosMode(7, 7);
    int x=0,y=0,w=0,h=0;
    PE_GetMessageWindowTextArea(7,&x,&y,&w,&h);
    assert(x==360 && y==600 && w==600 && h==118);
    Point position;
    assert(parts_message_window_render_text(&fixture,&position) == &fixture.message->text);
    assert(position.x==360 && position.y==600); /* No double origin offset. */

    PE_SetKeyWaitShow(7,true);
    assert(PE_IsKeyWaitShow(7));
    PE_SetKeyWaitShow(7,false);
    assert(!PE_IsKeyWaitShow(7) && fixture.global.show);
    for(int i=0;i<100;i++) {
        PE_SetMessageWindowText(7,NULL,0,NULL,0,0);
        assert_text(PE_GetMessageWindowText(7), "");
        assert(!parts_message_window_render_text(&fixture,&position));
        assert_text(PE_GetMessageWindowCGName(7), "new-background");
    }
    struct string *unknown=make_string("${unknown}x ${time",18);
    assert_text(message_window_plain_text(unknown), "${unknown}x ${time");
    free_string(unknown);
    parts_message_window_free(fixture.message);
    fixture.message=NULL;
    free_string(fixture.states[0].cg.name);
    free_string(captured_plain);
    assert(EMPTY_STRING.ref == empty_before);
    puts("message window: raw/plain GB18030, composer round-trip, CG/text isolation, geometry, wait visibility, ownership: PASS");
    return 0;
}
