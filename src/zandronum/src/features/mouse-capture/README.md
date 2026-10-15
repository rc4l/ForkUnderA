# mouse-capture

What the game does with the mouse each tic: whether relative motion aims, whether clicks reach the game, and whether the cursor shows. A desktop's SDL holds the mouse whenever the game asks, while a page's pointer lock comes and goes on its own (Escape drops it), and without it SDL reports where the cursor is rather than how it moved.

- `computation/mousecapture_compute.h`: `ComputeMouseCapture`, from whether the game wants the mouse and whether the window holds it, tested.

## In-engine hooks

- `posix/sdl/i_input.cpp`: `I_CheckNativeMouse` follows `ComputeMouseCapture`, held while the host (`posix/sdl/sdlhost.h`) says the window has the mouse, and the event pump reads motion and clicks by it.
