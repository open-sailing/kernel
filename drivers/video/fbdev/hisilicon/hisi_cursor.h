#ifndef HISI_CURSOR_H__
#define HISI_CURSOR_H__

void hw_cursor_enable(struct hisi_cursor *cursor);
void hw_cursor_disable(struct hisi_cursor *cursor);
void hw_cursor_set_size(struct hisi_cursor *cursor, int w, int h);
void hw_cursor_set_pos(struct hisi_cursor *cursor, int x, int y);
void hw_cursor_set_color(struct hisi_cursor *cursor, u32 fg, u32 bg);
void hw_cursor_set_data(struct hisi_cursor *cursor, u16 rop,
						const u8 *data, const u8 *mask);
#endif
