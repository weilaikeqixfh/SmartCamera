#ifndef APP_H
#define APP_H

void app_send_video_data(int fd);
void app_send_control_info(struct json_object *obj);

#endif
