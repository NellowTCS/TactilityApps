#include "tactility.h"
#include "duktape.h"

typedef struct {
    lv_obj_t *screen;
    int x, y;
    bool bold;
    lv_color_t color;
    int obj_x, obj_y;
    bool is_game;
    lv_obj_t *game_obj;
    duk_context *js_ctx;
    tt_file_t *html_file;
    lv_indev_t *touch_dev;
    lv_font_t *current_font; // For font-size
    lv_color_t bg_color;     // Background color
} html_app_t;

void app_init(tt_app_t *app);
void app_run(tt_app_t *app);
void app_stop(tt_app_t *app);
void parse_html(html_app_t *ctx, const char *html);
void render_text(html_app_t *ctx, const char *text, size_t len);
void handle_tag(html_app_t *ctx, const char *tag, size_t tag_len, bool closing, char *attr, size_t attr_len, const char *content, size_t content_len);
void game_loop(void *pvParameters);

// JS APIs
static duk_ret_t js_draw_rect(duk_context *ctx) {
    html_app_t *app_ctx = duk_get_context_data(ctx);
    int x = duk_get_int(ctx, 0);
    int y = duk_get_int(ctx, 1);
    int w = duk_get_int(ctx, 2);
    int h = duk_get_int(ctx, 3);
    lv_obj_t *rect = lv_obj_create(app_ctx->screen);
    lv_obj_set_size(rect, w, h);
    lv_obj_set_pos(rect, x, y);
    lv_obj_set_style_bg_color(rect, app_ctx->color, 0);
    return 0;
}

static duk_ret_t js_set_color(duk_context *ctx) {
    html_app_t *app_ctx = duk_get_context_data(ctx);
    const char *color = duk_get_string(ctx, 0);
    if (strcmp(color, "red") == 0) app_ctx->color = LV_COLOR_RED;
    else if (strcmp(color, "blue") == 0) app_ctx->color = LV_COLOR_BLUE;
    else if (strcmp(color, "white") == 0) app_ctx->color = LV_COLOR_WHITE;
    return 0;
}

static duk_ret_t js_set_bg_color(duk_context *ctx) {
    html_app_t *app_ctx = duk_get_context_data(ctx);
    const char *color = duk_get_string(ctx, 0);
    if (strcmp(color, "red") == 0) app_ctx->bg_color = LV_COLOR_RED;
    else if (strcmp(color, "blue") == 0) app_ctx->bg_color = LV_COLOR_BLUE;
    else if (strcmp(color, "white") == 0) app_ctx->bg_color = LV_COLOR_WHITE;
    lv_obj_set_style_bg_color(app_ctx->screen, app_ctx->bg_color, 0);
    return 0;
}

static duk_ret_t js_move_obj(duk_context *ctx) {
    html_app_t *app_ctx = duk_get_context_data(ctx);
    int x = duk_get_int(ctx, 0);
    int y = duk_get_int(ctx, 1);
    if (app_ctx->game_obj) {
        lv_obj_set_pos(app_ctx->game_obj, x, y);
        app_ctx->obj_x = x;
        app_ctx->obj_y = y;
    }
    return 0;
}

static duk_ret_t js_get_touch_x(duk_context *ctx) {
    html_app_t *app_ctx = duk_get_context_data(ctx);
    if (app_ctx->touch_dev) {
        lv_point_t point;
        lv_indev_get_point(app_ctx->touch_dev, &point);
        duk_push_int(ctx, point.x);
    } else {
        duk_push_int(ctx, -1); // No touch
    }
    return 1;
}

static duk_ret_t js_get_touch_y(duk_context *ctx) {
    html_app_t *app_ctx = duk_get_context_data(ctx);
    if (app_ctx->touch_dev) {
        lv_point_t point;
        lv_indev_get_point(app_ctx->touch_dev, &point);
        duk_push_int(ctx, point.y);
    } else {
        duk_push_int(ctx, -1);
    }
    return 1;
}

TT_APP(app, "html_js_app", app_init, app_run, app_stop);

void app_init(tt_app_t *app) {
    html_app_t *ctx = tt_calloc(1, sizeof(html_app_t));
    ctx->screen = lv_obj_create(NULL);
    ctx->x = 10; ctx->y = 10;
    ctx->bold = false;
    ctx->color = LV_COLOR_WHITE;
    ctx->bg_color = LV_COLOR_BLACK;
    ctx->is_game = false;
    ctx->game_obj = NULL;
    ctx->js_ctx = duk_create_heap_default();
    ctx->touch_dev = lv_indev_get_next(NULL); // May be NULL if no touch
    ctx->current_font = &lv_font_montserrat_14; // Default

    duk_push_c_function(ctx->js_ctx, js_draw_rect, 4);
    duk_put_global_string(ctx->js_ctx, "drawRect");
    duk_push_c_function(ctx->js_ctx, js_set_color, 1);
    duk_put_global_string(ctx->js_ctx, "setColor");
    duk_push_c_function(ctx->js_ctx, js_set_bg_color, 1);
    duk_put_global_string(ctx->js_ctx, "setBgColor");
    duk_push_c_function(ctx->js_ctx, js_move_obj, 2);
    duk_put_global_string(ctx->js_ctx, "moveObj");
    duk_push_c_function(ctx->js_ctx, js_get_touch_x, 0);
    duk_put_global_string(ctx->js_ctx, "getTouchX");
    duk_push_c_function(ctx->js_ctx, js_get_touch_y, 0);
    duk_put_global_string(ctx->js_ctx, "getTouchY");
    duk_push_pointer(ctx->js_ctx, ctx);
    duk_put_global_string(ctx->js_ctx, "__app_ctx");

    app->data = ctx;
}

void app_run(tt_app_t *app) {
    html_app_t *ctx = (html_app_t *)app->data;
    ctx->html_file = tt_file_open("/sdcard/index.html", "r");
    if (!ctx->html_file) {
        lv_label_set_text(lv_label_create(ctx->screen), "No index.html on SD");
        lv_scr_load(ctx->screen);
        return;
    }
    size_t file_size = tt_file_size(ctx->html_file);
    char *html = tt_calloc(1, file_size + 1);
    tt_file_read(ctx->html_file, html, file_size);
    html[file_size] = '\0';
    tt_file_close(ctx->html_file);
    parse_html(ctx, html);
    tt_free(html);
    if (ctx->is_game) {
        tt_task_create(game_loop, "game_loop", 4096, ctx, 5, NULL);
    }
    lv_scr_load(ctx->screen);
}

void app_stop(tt_app_t *app) {
    html_app_t *ctx = (html_app_t *)app->data;
    duk_destroy_heap(ctx->js_ctx);
    lv_obj_clean(ctx->screen);
    tt_free(ctx);
}

void parse_html(html_app_t *ctx, const char *html) {
    size_t i = 0, tag_len = 0, attr_len = 0, content_len = 0;
    char tag[32], attr[64], content[256];
    bool in_tag = false, in_attr = false, in_content = false, closing = false;

    while (html[i]) {
        if (!in_tag && html[i] == '<') {
            in_tag = true;
            tag_len = attr_len = content_len = 0;
            closing = false;
            i++;
            continue;
        }
        if (in_tag) {
            if (html[i] == '/') closing = true;
            else if (html[i] == ' ' && tag_len > 0) in_attr = true;
            else if (html[i] == '>') {
                in_tag = false;
                in_attr = false;
                if (!closing) in_content = true;
                else handle_tag(ctx, tag, tag_len, closing, attr, attr_len, content, content_len);
            } else if (in_attr && attr_len < sizeof(attr) - 1) attr[attr_len++] = html[i];
            else if (tag_len < sizeof(tag) - 1) tag[tag_len++] = html[i];
        } else if (in_content) {
            if (html[i] == '<' && html[i + 1] == '/') {
                in_content = false;
                handle_tag(ctx, tag, tag_len, closing, attr, attr_len, content, content_len);
                i += 2;
                closing = true;
            } else if (content_len < sizeof(content) - 1) {
                content[content_len++] = html[i];
            }
        } else if (html[i] != '<') {
            render_text(ctx, &html[i], 1);
        }
        i++;
    }
}

void render_text(html_app_t *ctx, const char *text, size_t len) {
    if (ctx->y > 240) return;
    lv_obj_t *label = lv_label_create(ctx->screen);
    char text_buf[2] = {text[0], '\0'};
    lv_label_set_text(label, text_buf);
    lv_obj_set_pos(label, ctx->x, ctx->y);
    lv_obj_set_style_text_color(label, ctx->bold ? LV_COLOR_YELLOW : ctx->color, 0);
    lv_obj_set_style_text_font(label, ctx->current_font, 0);
    lv_obj_set_style_bg_color(label, ctx->bg_color, 0);
    ctx->x += 8;
}

void handle_tag(html_app_t *ctx, const char *tag, size_t tag_len, bool closing, char *attr, size_t attr_len, const char *content, size_t content_len) {
    char tag_str[32], attr_str[64], content_str[256];
    strncpy(tag_str, tag, tag_len); tag_str[tag_len] = '\0';
    strncpy(attr_str, attr, attr_len); attr_str[attr_len] = '\0';
    strncpy(content_str, content, content_len); content_str[content_len] = '\0';

    int width = 10, height = 10;
    if (attr_len) {
        char *style = strstr(attr_str, "style=\"");
        if (style) {
            char *color = strstr(style, "color:");
            char *bg = strstr(style, "background-color:");
            char *fs = strstr(style, "font-size:");
            char *w = strstr(style, "width:");
            char *h = strstr(style, "height:");
            if (color) {
                if (strstr(color, "red")) ctx->color = LV_COLOR_RED;
                else if (strstr(color, "blue")) ctx->color = LV_COLOR_BLUE;
                else if (strstr(color, "white")) ctx->color = LV_COLOR_WHITE;
            }
            if (bg) {
                if (strstr(bg, "red")) ctx->bg_color = LV_COLOR_RED;
                else if (strstr(bg, "blue")) ctx->bg_color = LV_COLOR_BLUE;
                else if (strstr(bg, "white")) ctx->bg_color = LV_COLOR_WHITE;
            }
            if (fs) {
                int size = atoi(fs + 9);
                if (size <= 14) ctx->current_font = &lv_font_montserrat_14;
                else if (size <= 28) ctx->current_font = &lv_font_montserrat_28;
                else ctx->current_font = &lv_font_montserrat_48;
            }
            if (w) width = atoi(w + 6);
            if (h) height = atoi(h + 7);
        }
    }

    if (strcmp(tag_str, "b") == 0) {
        ctx->bold = !closing;
    } else if (strcmp(tag_str, "p") == 0 && !closing) {
        ctx->x = 10;
        ctx->y += 20;
    } else if (strcmp(tag_str, "h1") == 0 && !closing) {
        ctx->x = 10;
        ctx->y += 30;
        lv_obj_t *label = lv_label_create(ctx->screen);
        lv_label_set_text(label, content_str);
        lv_obj_set_pos(label, ctx->x, ctx->y);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(label, ctx->color, 0);
        lv_obj_set_style_bg_color(label, ctx->bg_color, 0);
        ctx->y += 40;
    } else if (strcmp(tag_str, "br") == 0) {
        ctx->x = 10;
        ctx->y += 20;
    } else if (strcmp(tag_str, "div") == 0 && !closing) {
        ctx->obj_x = ctx->x;
        ctx->obj_y = ctx->y;
        if (attr_len) {
            char *x = strstr(attr_str, "x=\"");
            char *y = strstr(attr_str, "y=\"");
            if (x) ctx->obj_x = atoi(x + 3);
            if (y) ctx->obj_y = atoi(y + 3);
        }
        ctx->game_obj = lv_obj_create(ctx->screen);
        lv_obj_set_size(ctx->game_obj, width, height);
        lv_obj_set_pos(ctx->game_obj, ctx->obj_x, ctx->obj_y);
        lv_obj_set_style_bg_color(ctx->game_obj, ctx->color, 0);
        ctx->is_game = true;
    } else if (strcmp(tag_str, "img") == 0 && !closing) {
        char *src = strstr(attr_str, "src=\"");
        if (src) {
            char img_path[64];
            strncpy(img_path, src + 5, strstr(src, "\"") - (src + 5));
            img_path[strstr(src, "\"") - (src + 5)] = '\0';
            tt_file_t *img_file = tt_file_open(img_path, "r");
            if (img_file) {
                size_t img_size = tt_file_size(img_file);
                uint8_t *img_data = tt_calloc(1, img_size);
                tt_file_read(img_file, img_data, img_size);
                
                // BMP header parsing
                lv_img_dsc_t img_dsc;
                img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
                img_dsc.header.w = *(uint16_t*)(img_data + 18); // Width at offset 18
                img_dsc.header.h = *(uint16_t*)(img_data + 22); // Height at offset 22
                img_dsc.data = img_data + *(uint32_t*)(img_data + 10); // Data offset at 10
                if (width > 0 && height > 0) { // Override with style if set
                    img_dsc.header.w = width;
                    img_dsc.header.h = height;
                }
                
                lv_obj_t *img = lv_img_create(ctx->screen);
                lv_img_set_src(img, &img_dsc);
                lv_obj_set_pos(img, ctx->x, ctx->y);
                tt_file_close(img_file);
                tt_free(img_data);
                ctx->x += img_dsc.header.w;
            }
        }
    } else if (strcmp(tag_str, "a") == 0 && !closing) {
        lv_obj_t *label = lv_label_create(ctx->screen);
        lv_label_set_text(label, content_str);
        lv_obj_set_pos(label, ctx->x, ctx->y);
        lv_obj_set_style_text_color(label, LV_COLOR_BLUE, 0);
        lv_obj_set_style_text_font(label, ctx->current_font, 0);
        lv_obj_set_style_bg_color(label, ctx->bg_color, 0);
        ctx->x += strlen(content_str) * 8;
    } else if (strcmp(tag_str, "button") == 0 && !closing) {
        lv_obj_t *btn = lv_btn_create(ctx->screen);
        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, content_str);
        lv_obj_set_pos(btn, ctx->x, ctx->y);
        lv_obj_set_size(btn, width, height);
        lv_obj_set_style_bg_color(btn, ctx->color, 0);
        lv_obj_set_style_text_color(label, LV_COLOR_WHITE, 0);
        lv_obj_add_event_cb(btn, [](lv_event_t *e) {
            html_app_t *ctx = (html_app_t*)lv_event_get_user_data(e);
            duk_eval_string(ctx->js_ctx, "if (typeof onButtonClick === 'function') onButtonClick();");
        }, LV_EVENT_CLICKED, ctx);
        ctx->x += width;
    } else if (strcmp(tag_str, "game") == 0 && !closing) {
        ctx->is_game = true;
    } else if (strcmp(tag_str, "script") == 0 && !closing) {
        if (content_len) {
            duk_eval_string(ctx->js_ctx, content_str);
        }
    }
}

void game_loop(void *pvParameters) {
    html_app_t *ctx = (html_app_t *)pvParameters;
    while (1) {
        lv_task_handler();
        if (ctx->game_obj && ctx->touch_dev) {
            lv_point_t point;
            lv_indev_get_point(ctx->touch_dev, &point);
            if (lv_indev_is_active(ctx->touch_dev)) {
                lv_obj_set_pos(ctx->game_obj, point.x - 5, point.y - 5);
                ctx->obj_x = point.x - 5;
                ctx->obj_y = point.y - 5;
            }
        }
        tt_task_delay(16);
    }
}
