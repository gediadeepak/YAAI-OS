#ifndef YAAI_VIRTIO_INPUT_H
#define YAAI_VIRTIO_INPUT_H
void virtio_input_initialize(unsigned width, unsigned height);
void virtio_input_poll(void);
int virtio_input_keyboard_available(void);
int virtio_input_pointer_available(void);
#endif
