#ifndef _OSUU_MATH_H
#define _OSUU_MATH_H
// Doom's game logic is fixed-point. The few float uses (mouse acceleration,
// float config values, timedemo FPS) are patched out under OSUU, so this only
// has to satisfy the #include. Using float/double anywhere else fails to
// link, which is intentional: the build is -mgeneral-regs-only.
double fabs(double x);
#endif
