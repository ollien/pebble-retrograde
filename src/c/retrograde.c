#include "gcolor_definitions.h"
#include <pebble.h>

static Window *s_window;
static Layer *s_time_layer;

static GBitmap *s_pebble_logo;
static BitmapLayer *s_pebble_logo_layer;

static GPathInfo *s_hour_hand_path_info;
static GPath *s_hour_hand_path;
static GPathInfo *s_hour_hand_thin_path_info;
static GPath *s_hour_hand_thin_path;

static GPathInfo *s_minute_hand_path_info;
static GPath *s_minute_hand_path;
static GPathInfo *s_minute_hand_thin_path_info;
static GPath *s_minute_hand_thin_path;

static uint32_t s_hand_width = 8;
static uint32_t s_arc_width = 24;
static uint32_t s_min_angle_deg = 35;
static uint32_t s_max_angle_deg = 35 + 110;

static GColor s_background_color = GColorBlack;
static GColor s_arc_color = GColorLightGray;
static GColor s_hour_hand_color = GColorLiberty;
static GColor s_minute_hand_color = GColorCobaltBlue;

static GPathInfo *create_rectangle_path_info(int length, int width) {
  GPathInfo *info = malloc(sizeof(GPathInfo));
  info->num_points = 4;
  info->points = malloc(sizeof(GPoint) * 4);

  info->points[0] = GPoint(-width / 2, 0);
  info->points[1] = GPoint(width / 2, 0);
  info->points[2] = GPoint(width / 2, length);
  info->points[3] = GPoint(-width / 2, length);

  return info;
}

static void prv_free_gpath_info(GPathInfo *info) {
  free(info->points);
  free(info);
}

static GPoint prv_origin(GRect rect) {
  return GPoint(rect.size.w / 8, rect.size.h / 2);
}

static uint32_t prv_angle_deg(uint16_t angle) {
  return TRIG_MAX_ANGLE * angle / 360;
}

static uint32_t prv_hand_angle(uint16_t time_component, uint16_t max) {
  return s_min_angle_deg +
         (time_component * (s_max_angle_deg - s_min_angle_deg)) / max;
}

static uint32_t prv_arc_radius(GRect rect) { return rect.size.h / 2; }

static uint32_t prv_hour_hand_length(GRect rect) {
  return prv_arc_radius(rect) - s_arc_width / 2 - s_hand_width / 4;
}

static uint32_t prv_minute_hand_length(GRect rect) {
  return prv_arc_radius(rect) + s_arc_width / 2 - s_hand_width / 4 + 1;
}

static GPoint prv_arc_edge_at_angle(GRect rect, uint32_t angle) {
  GPoint origin = prv_origin(rect);
  uint32_t radius = prv_arc_radius(rect);

  int32_t x = origin.x -
              (sin_lookup(angle) * (radius + s_arc_width / 2) / TRIG_MAX_RATIO);
  int32_t y = origin.y +
              (cos_lookup(angle) * (radius + s_arc_width / 2) / TRIG_MAX_RATIO);

  return GPoint(x, y);
}

static GPoint prv_arc_point_at_angle(GRect rect, uint32_t angle,
                                     uint32_t edge_offset) {
  GPoint origin = prv_origin(rect);
  uint32_t radius = prv_arc_radius(rect);

  int32_t x =
      origin.x - (sin_lookup(angle) * (radius + s_arc_width / 2 - edge_offset) /
                  TRIG_MAX_RATIO);
  int32_t y =
      origin.y + (cos_lookup(angle) * (radius + s_arc_width / 2 - edge_offset) /
                  TRIG_MAX_RATIO);

  return GPoint(x, y);
}

static void prv_draw_main_arc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, s_background_color);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  GPoint origin = prv_origin(bounds);
  uint32_t radius = prv_arc_radius(bounds);

  GRect arc_rect =
      GRect(origin.x - radius, origin.y - radius, radius * 2, radius * 2);

  graphics_context_set_stroke_color(ctx, s_arc_color);
  graphics_context_set_stroke_width(ctx, s_arc_width);
  graphics_context_set_fill_color(ctx, s_arc_color);
  graphics_draw_arc(ctx, arc_rect, GOvalScaleModeFitCircle, 0,
                    TRIG_MAX_ANGLE / 2);

  uint32_t cap_width = origin.x;
  GRect top_arc_cap = GRect(0, 0, cap_width, s_arc_width / 2);
  graphics_context_set_stroke_color(ctx, s_arc_color);
  graphics_context_set_fill_color(ctx, s_arc_color);
  graphics_fill_rect(ctx, top_arc_cap, 0, GCornerNone);

  GRect bottom_arc_cap =
      GRect(0, bounds.size.h - s_arc_width / 2, cap_width, s_arc_width / 2);
  graphics_context_set_stroke_color(ctx, s_arc_color);
  graphics_context_set_fill_color(ctx, s_arc_color);
  graphics_fill_rect(ctx, bottom_arc_cap, 0, GCornerNone);
}

static void prv_draw_hand(Layer *layer, GContext *ctx, GPath *path,
                          GPath *thin_path, uint32_t angle, uint16_t length,
                          GColor color) {
  GRect bounds = layer_get_bounds(layer);
  GPoint origin = prv_origin(bounds);

  // Horizontal lines look a bit wider, so we use the "thin" path for that.
  GPath *actual_path = angle == prv_angle_deg(270) ? thin_path : path;

  graphics_context_set_fill_color(ctx, color);
  gpath_move_to(actual_path, origin);
  gpath_rotate_to(actual_path, angle);
  gpath_draw_filled(ctx, actual_path);

  int tip_x = origin.x - (sin_lookup(angle) * length / TRIG_MAX_RATIO);
  int tip_y = origin.y + (cos_lookup(angle) * length / TRIG_MAX_RATIO);
  graphics_fill_circle(ctx, GPoint(tip_x, tip_y), s_hand_width / 4);
}

static void prv_draw_hour_tick_mark_label(Layer *layer, GContext *ctx, int i) {
  GRect bounds = layer_get_bounds(layer);
  GPoint origin = prv_origin(bounds);
  uint32_t radius = prv_arc_radius(bounds);
  uint32_t angle = prv_angle_deg(180) + prv_angle_deg(prv_hand_angle(i, 12));

  char hour_buf[3];
  snprintf(hour_buf, sizeof(hour_buf), "%d", i == 0 ? 12 : i);
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  uint32_t text_radius = radius + s_arc_width / 2 - 15;
  int32_t text_x =
      origin.x - (sin_lookup(angle) * text_radius / TRIG_MAX_RATIO);
  int32_t text_y =
      origin.y + (cos_lookup(angle) * text_radius / TRIG_MAX_RATIO);
  GRect hour_rect = GRect(text_x - 12, text_y - 12, 24, 24);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, hour_buf, font, hour_rect, GTextOverflowModeWordWrap,
                     GTextAlignmentCenter, NULL);
}

static void prv_draw_hour_tick_mark(Layer *layer, GContext *ctx, int i) {
  GRect bounds = layer_get_bounds(layer);
  GPoint origin = prv_origin(bounds);
  uint32_t radius = prv_arc_radius(bounds);
  uint32_t angle = prv_angle_deg(180) + prv_angle_deg(prv_hand_angle(i, 12));
  GPoint edge_point = prv_arc_edge_at_angle(bounds, angle);
  GPoint inner_point = prv_arc_point_at_angle(bounds, angle, 4);

  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_line(ctx, inner_point, edge_point);
}

static void prv_draw_minute_line_label(Layer *layer, GContext *ctx, int i) {
  GRect bounds = layer_get_bounds(layer);
  GPoint origin = prv_origin(bounds);
  uint32_t radius = prv_arc_radius(bounds);
  uint32_t angle = prv_angle_deg(180) + prv_angle_deg(prv_hand_angle(i, 12));

  GPoint edge_point = prv_arc_edge_at_angle(bounds, angle);

  char min_buf[3];
  snprintf(min_buf, sizeof(min_buf), "%d", i * 5);
  GFont min_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  GRect min_rect = GRect(bounds.size.w - 24, edge_point.y - 15, 24, 14);
  graphics_context_set_text_color(ctx, s_arc_color);
  graphics_draw_text(ctx, min_buf, min_font, min_rect,
                     GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
}

static void prv_draw_minute_line(Layer *layer, GContext *ctx, int i) {
  GRect bounds = layer_get_bounds(layer);
  GPoint origin = prv_origin(bounds);
  uint32_t radius = prv_arc_radius(bounds);
  uint32_t angle = prv_angle_deg(180) + prv_angle_deg(prv_hand_angle(i, 12));
  GPoint edge_point = prv_arc_edge_at_angle(bounds, angle);
  GPoint end_point = GPoint(bounds.size.w, edge_point.y);

  graphics_context_set_stroke_color(ctx, s_arc_color);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_line(ctx, edge_point, end_point);
}

static void prv_draw_hour_ticks(Layer *layer, GContext *ctx) {
  for (int i = 0; i <= 12; i++) {
    prv_draw_hour_tick_mark(layer, ctx, i);
    prv_draw_hour_tick_mark_label(layer, ctx, i);
  }
}

static void prv_draw_minute_lines(Layer *layer, GContext *ctx) {
  for (int i = 0; i <= 12; i++) {
    prv_draw_minute_line(layer, ctx, i);
    prv_draw_minute_line_label(layer, ctx, i);
  }
}

static void prv_draw_face_cirlces(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  GPoint origin = prv_origin(bounds);

  graphics_context_set_stroke_color(ctx, GColorDarkGray);
  graphics_context_set_stroke_width(ctx, 1);
  for (int i = 2; i <= 7; i++) {
    graphics_draw_circle(ctx, origin, prv_arc_radius(bounds) * i / 9);
  }
}

static void prv_draw_face_layer(Layer *layer, GContext *ctx) {
  prv_draw_main_arc(layer, ctx);
  prv_draw_face_cirlces(layer, ctx);
  prv_draw_minute_lines(layer, ctx);
  prv_draw_hour_ticks(layer, ctx);
}

static void prv_draw_pin(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  GPoint origin = prv_origin(bounds);

  // outer ring
  graphics_context_set_fill_color(ctx, s_minute_hand_color);
  graphics_fill_circle(ctx, origin, s_hand_width / 2 + 2);

  // inner dot
  graphics_context_set_fill_color(ctx, GColorBabyBlueEyes);
  graphics_fill_circle(ctx, origin, s_hand_width / 4);
}

static void prv_draw_hands(Layer *layer, GContext *ctx, tm *time) {
  GRect bounds = layer_get_bounds(layer);
  uint32_t hour = time->tm_hour % 12;
  uint32_t minute = time->tm_min;

  uint32_t hour_angle =
      prv_angle_deg(180) +
      prv_angle_deg(prv_hand_angle(hour * 60 + minute, 12 * 60));
  uint32_t minute_angle =
      prv_angle_deg(180) + prv_angle_deg(prv_hand_angle(minute, 60));
  uint32_t h_length = prv_hour_hand_length(bounds);
  uint32_t m_length = prv_minute_hand_length(bounds);

  prv_draw_hand(layer, ctx, s_hour_hand_path, s_hour_hand_thin_path, hour_angle,
                h_length, s_hour_hand_color);
  prv_draw_hand(layer, ctx, s_minute_hand_path, s_minute_hand_thin_path,
                minute_angle, m_length, s_minute_hand_color);
  prv_draw_pin(layer, ctx);
}

static void prv_draw_date_circle(Layer *layer, GContext *ctx, tm *time) {
  GRect bounds = layer_get_bounds(layer);
  GPoint arc_edge_point = prv_arc_edge_at_angle(bounds, prv_angle_deg(270));
  uint32_t center_x = (bounds.size.w + arc_edge_point.x - 9) / 2;
  uint32_t center_y = arc_edge_point.y;

  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_context_set_stroke_color(ctx, GColorDarkGray);
  graphics_draw_circle(ctx, GPoint(center_x, center_y), 14);
  graphics_fill_circle(ctx, GPoint(center_x, center_y), 14);

  char day_buf[3];
  snprintf(day_buf, sizeof(day_buf), "%d", time->tm_mday);
  GFont day_font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  GRect day_rect = GRect(center_x - 12, center_y - 16, 24, 24);
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, day_buf, day_font, day_rect,
                     GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

static void prv_draw_time_layer(Layer *layer, GContext *ctx) {
  time_t now = time(NULL);
  struct tm *time = localtime(&now);

  prv_draw_hands(layer, ctx, time);
  prv_draw_date_circle(layer, ctx, time);
}

static void prv_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  layer_set_update_proc(window_layer, prv_draw_face_layer);

  s_hour_hand_path_info =
      create_rectangle_path_info(prv_hour_hand_length(bounds), s_hand_width);
  s_hour_hand_path = gpath_create(s_hour_hand_path_info);

  s_hour_hand_thin_path_info = create_rectangle_path_info(
      prv_hour_hand_length(bounds), s_hand_width - 3);
  s_hour_hand_thin_path = gpath_create(s_hour_hand_thin_path_info);

  s_minute_hand_path_info =
      create_rectangle_path_info(prv_minute_hand_length(bounds), s_hand_width);
  s_minute_hand_path = gpath_create(s_minute_hand_path_info);

  s_minute_hand_thin_path_info = create_rectangle_path_info(
      prv_minute_hand_length(bounds), s_hand_width - 3);
  s_minute_hand_thin_path = gpath_create(s_minute_hand_thin_path_info);
}

static void prv_tick_handler(tm *_tick_time, TimeUnits _units_changed) {
  layer_mark_dirty(s_time_layer);
}

static void prv_window_unload(Window *window) {
  gpath_destroy(s_hour_hand_path);
  prv_free_gpath_info(s_hour_hand_path_info);

  gpath_destroy(s_hour_hand_thin_path);
  prv_free_gpath_info(s_hour_hand_thin_path_info);

  gpath_destroy(s_minute_hand_path);
  prv_free_gpath_info(s_minute_hand_path_info);

  gpath_destroy(s_minute_hand_thin_path);
  prv_free_gpath_info(s_minute_hand_thin_path_info);

  bitmap_layer_destroy(s_pebble_logo_layer);
  gbitmap_destroy(s_pebble_logo);
}

static void prv_init(void) {
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){
                                           .load = prv_window_load,
                                           .unload = prv_window_unload,
                                       });

  Layer *window_layer = window_get_root_layer(s_window);
  GRect bounds = layer_get_bounds(window_layer);
  s_time_layer = layer_create(bounds);

  tick_timer_service_subscribe(SECOND_UNIT, prv_tick_handler);
  layer_set_update_proc(s_time_layer, prv_draw_time_layer);
  layer_add_child(window_layer, s_time_layer);

  window_stack_push(s_window, false);
}

static void prv_deinit(void) { window_destroy(s_window); }

int main(void) {
  prv_init();

#ifdef DEBUG
  light_enable(true);
#endif

  app_event_loop();
  prv_deinit();
}
