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

// How much wider than tall the TV shows each pixel of that picture: 4/3 when
// a 16:9 set stretches it, 1 otherwise. The render engine widens its
// horizontal field of view (and culling) by as much.
VX_EXPORT void VxWiiSetPixelAspect(float aspect);
VX_EXPORT float VxWiiGetPixelAspect();

// Text entry. Blocks that read typed text call VxWiiRequestTextInput every
// frame they run; the input manager shows the on-screen keyboard while the
// requests keep coming. VxWiiConsumeTextInputRequest reports whether text was
// requested since its previous call.
VX_EXPORT void VxWiiRequestTextInput();
VX_EXPORT XBOOL VxWiiConsumeTextInputRequest();

#endif // VXWIIPLATFORM_H
