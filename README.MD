# Framingham
One day I decided to learn the wayland protocol by just writing my own client from scratch in C. Here is the result.

# Details
It just draws a scrolling RGB rainbow, the plan is more to understand the protocol than deal with content. The "rendering" is done by a horrible `for` loop running entirely on the CPU, don't make the window too big for your own sake.

# Future "plans"
When I feel like it, I might:
- [ ] Convert this to use glib/gobject instead
- [ ] Experiment with graphics APIs

# Compiling
Needs meson, a c compiler, `wayland-client` and `wayland-protocols`.
