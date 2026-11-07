#include "hml_project.h"
#include "hml_tag.h"
#include "hml_driver.h"
#include "hml_ui.h"
#include "hml_os.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static hml_project_info_t s_info;
static cJSON *s_root;

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if(!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if(n < 0) { fclose(f); return NULL; }
    char *buf = malloc((size_t)n + 1);
    if(!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[rd] = '\0';
    /* skip UTF-8 BOM */
    if(rd >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) {
        memmove(buf, buf + 3, rd - 2);
    }
    return buf;
}

static cJSON *parse_json_file(const char *path)
{
    char *text = read_file(path);
    if(!text) { HML_LOG_ERROR("cannot read '%s'", path); return NULL; }
    cJSON *j = cJSON_Parse(text);
    if(!j) {
        const char *err = cJSON_GetErrorPtr();
        long line = 1;
        if(err) for(const char *p = text; p < err; p++) if(*p == '\n') line++;
        HML_LOG_ERROR("JSON syntax error in '%s' near line %ld", path, line);
    }
    free(text);
    return j;
}

static void resolve_path(char *out, size_t n, const char *rel)
{
    if(!rel) { out[0] = '\0'; return; }
    if(rel[0] == '/' || rel[0] == '\\' || (rel[0] && rel[1] == ':')) snprintf(out, n, "%s", rel);
    else snprintf(out, n, "%s/%s", s_info.dir, rel);
}

static void create_tags(const cJSON *tags)
{
    if(!cJSON_IsArray(tags)) { HML_LOG_WARN("project has no \"tags\" array"); return; }
    const cJSON *t;
    int n = 0;
    cJSON_ArrayForEach(t, tags) {
        const char *name = hml_json_str(t, "name", NULL);
        if(!name) { HML_LOG_WARN("tag without \"name\" skipped"); continue; }
        hml_type_t type = hml_type_from_name(hml_json_str(t, "type", NULL), HML_TYPE_FLOAT);
        const cJSON *init = cJSON_GetObjectItemCaseSensitive(t, "init");
        hml_value_t v;
        if(cJSON_IsString(init)) hml_value_set_str(&v, init->valuestring);
        else if(cJSON_IsBool(init)) hml_value_set_num(&v, HML_TYPE_BOOL, cJSON_IsTrue(init));
        else hml_value_set_num(&v, HML_TYPE_FLOAT, cJSON_IsNumber(init) ? init->valuedouble : 0);
        bool has_limits = hml_json_has(t, "min") || hml_json_has(t, "max");
        double min = hml_json_num(t, "min", -1e300), max = hml_json_num(t, "max", 1e300);
        if(hml_tag_create(name, type, &v, has_limits, min, max)) n++;
    }
    HML_LOG_INFO("%d tags created", n);
}

int hml_project_load(const char *path)
{
    memset(&s_info, 0, sizeof(s_info));
    snprintf(s_info.path, sizeof(s_info.path), "%s", path);
    snprintf(s_info.dir, sizeof(s_info.dir), "%s", path);
    char *slash = strrchr(s_info.dir, '/');
    char *bslash = strrchr(s_info.dir, '\\');
    if(bslash && (!slash || bslash > slash)) slash = bslash;
    if(slash) *slash = '\0';
    else snprintf(s_info.dir, sizeof(s_info.dir), ".");

    s_root = parse_json_file(path);
    if(!s_root) return -1;

    snprintf(s_info.title, sizeof(s_info.title), "%s", hml_json_str(s_root, "title", "HML Runtime"));
    const cJSON *disp = cJSON_GetObjectItemCaseSensitive(s_root, "display");
    s_info.width = (int)hml_json_num(disp, "width", 800);
    s_info.height = (int)hml_json_num(disp, "height", 480);
    s_info.zoom = hml_json_num(disp, "zoom", 1.0);
    s_info.rotation = (int)hml_json_num(disp, "rotation", 0);
    if(s_info.rotation % 90 != 0 || s_info.rotation < 0 || s_info.rotation > 270) {
        HML_LOG_WARN("display.rotation must be 0, 90, 180 or 270 (got %d); using 0", s_info.rotation);
        s_info.rotation = 0;
    }
    snprintf(s_info.fbdev, sizeof(s_info.fbdev), "%s", hml_json_str(disp, "fbdev", ""));
    snprintf(s_info.drm, sizeof(s_info.drm), "%s", hml_json_str(disp, "drm", ""));
    snprintf(s_info.input, sizeof(s_info.input), "%s", hml_json_str(disp, "input", ""));
    const cJSON *touch = disp ? cJSON_GetObjectItemCaseSensitive(disp, "touch") : NULL;
    s_info.touch_swap_axes = hml_json_bool(touch, "swap_axes", false);
    const cJSON *calib = touch ? cJSON_GetObjectItemCaseSensitive(touch, "calibration") : NULL;
    if(cJSON_IsArray(calib) && cJSON_GetArraySize(calib) == 4) {
        s_info.touch_has_calib = true;
        for(int i = 0; i < 4; i++) s_info.touch_calib[i] = (int)cJSON_GetArrayItem(calib, i)->valuedouble;
    }

    const cJSON *theme = cJSON_GetObjectItemCaseSensitive(s_root, "theme");
    s_info.dark = hml_json_bool(theme, "dark", true);
    s_info.primary = hml_ui_parse_color(hml_json_str(theme, "primary", NULL), lv_palette_main(LV_PALETTE_BLUE));
    s_info.secondary = hml_ui_parse_color(hml_json_str(theme, "secondary", NULL), lv_palette_main(LV_PALETTE_RED));
    snprintf(s_info.start_screen, sizeof(s_info.start_screen), "%s", hml_json_str(s_root, "start_screen", ""));

    create_tags(cJSON_GetObjectItemCaseSensitive(s_root, "tags"));
    HML_LOG_INFO("project '%s' loaded (%dx%d)", s_info.title, s_info.width, s_info.height);
    return 0;
}

const hml_project_info_t *hml_project_info(void)
{
    return &s_info;
}

void hml_project_apply_theme(lv_display_t *disp)
{
    const cJSON *theme = cJSON_GetObjectItemCaseSensitive(s_root, "theme");
    const lv_font_t *font = hml_json_has(theme, "font") ? hml_ui_font(cJSON_GetObjectItemCaseSensitive(theme, "font")) : LV_FONT_DEFAULT;
    lv_theme_t *th = lv_theme_default_init(disp, s_info.primary, s_info.secondary, s_info.dark, font);
    lv_display_set_theme(disp, th);
}

int hml_project_start_drivers(void)
{
    return hml_drivers_load(cJSON_GetObjectItemCaseSensitive(s_root, "drivers"));
}

int hml_project_build_screens(void)
{
    char current[64] = "";
    if(hml_ui_current_screen()) snprintf(current, sizeof(current), "%s", hml_ui_current_screen());
    hml_ui_unload_all();

    /* re-read the project file too so screen lists / start screen edits are picked up */
    cJSON *fresh = parse_json_file(s_info.path);
    if(fresh) {
        cJSON_Delete(s_root);
        s_root = fresh;
        snprintf(s_info.start_screen, sizeof(s_info.start_screen), "%s", hml_json_str(s_root, "start_screen", ""));
    }

    const cJSON *screens = cJSON_GetObjectItemCaseSensitive(s_root, "screens");
    int built = 0;
    char first[64] = "";
    if(cJSON_IsArray(screens)) {
        const cJSON *s;
        cJSON_ArrayForEach(s, screens) {
            cJSON *owned = NULL;
            const cJSON *def = s;
            if(cJSON_IsString(s)) {
                char path[640];
                resolve_path(path, sizeof(path), s->valuestring);
                owned = parse_json_file(path);
                def = owned;
                if(!owned) continue;
            }
            lv_obj_t *obj = hml_ui_build_screen(def, s_info.dir);
            if(obj) {
                if(!first[0]) snprintf(first, sizeof(first), "%s", hml_json_str(def, "name", ""));
                built++;
            }
            cJSON_Delete(owned);
        }
    }
    if(built == 0) {
        HML_LOG_ERROR("no screens could be built");
        /* keep something visible */
        lv_obj_t *scr = lv_screen_active();
        lv_obj_t *l = lv_label_create(scr);
        lv_label_set_text(l, "HML: no screens loaded\n(check the console log, press F5 to retry)");
        lv_obj_center(l);
        return 0;
    }

    const char *show = current[0] ? current : (s_info.start_screen[0] ? s_info.start_screen : first);
    if(!hml_ui_show_screen(show, "none")) hml_ui_show_screen(first, "none");
    hml_tags_dispatch(true);
    HML_LOG_INFO("%d screens built, showing '%s'", built, hml_ui_current_screen());
    return built;
}

void hml_project_unload(void)
{
    cJSON_Delete(s_root);
    s_root = NULL;
}
