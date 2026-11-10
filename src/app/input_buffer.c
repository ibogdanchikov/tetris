#include "app/input_buffer.h"

void input_buffer_key_down(InputBuffer *buffer, uint32_t action)
{
    buffer->held |= action;
    buffer->pending_pressed |= action;
}

void input_buffer_key_up(InputBuffer *buffer, uint32_t action)
{
    buffer->held &= ~action;
}

GameInput input_buffer_take(InputBuffer *buffer)
{
    const GameInput input = {
        .held = buffer->held | buffer->pending_pressed,
        .pressed = buffer->pending_pressed,
    };
    buffer->pending_pressed = 0;
    return input;
}

void input_buffer_reset(InputBuffer *buffer)
{
    buffer->held = 0;
    buffer->pending_pressed = 0;
}
