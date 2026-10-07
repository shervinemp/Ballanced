#ifndef VXWIIPLATFORM_H
#define VXWIIPLATFORM_H

#include "VxMathDefines.h"

// Console environment the Wii player publishes to VxMath before starting the
// engine. There is no window system: the "window" is the TV picture.

// Directory the game runs from (where boot.dol and base.cmo live), e.g. "sd:/apps/ballance/".
VX_EXPORT void VxWiiSetApplicationPath(const char *path);
VX_EXPORT const char *VxWiiGetApplicationPath();

// Size of the rendered picture (the embedded frame buffer), e.g. 640x480 or 640x528.
VX_EXPORT void VxWiiSetDisplaySize(int width, int height);
VX_EXPORT void VxWiiGetDisplaySize(int *width, int *height);

#endif // VXWIIPLATFORM_H
