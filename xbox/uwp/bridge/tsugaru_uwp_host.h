#ifndef TSUGARU_UWP_HOST_IS_INCLUDED
#define TSUGARU_UWP_HOST_IS_INCLUDED
/* Interface between the Tsugaru UWP wrapper of fssimplewindow (fsuwpwrapper.cpp) and the UWP host application.
   The host (UI thread) feeds input into the wrapper; the VM-side window connection hands finished frames to the host.
   All functions are thread-safe.  BSD 3-clause, like the rest of the emulator. */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- host -> wrapper (any thread) ---- */

/* A Windows virtual-key code (Windows.System.VirtualKey values are the same numbers). */
void TsugaruUwpKeyEvent(int virtualKey,int down);
/* A typed Unicode character. */
void TsugaruUwpCharEvent(int unicode);
/* Mouse position in the coordinate system of the picture the VM draws (picture pixels, top-left origin). */
void TsugaruUwpMouseMove(int x,int y);
/* button: 0 left, 1 middle, 2 right. */
void TsugaruUwpMouseButton(int button,int down,int x,int y);
/* Releases every key and button (focus lost, game paused). */
void TsugaruUwpResetInput(void);
/* Size FsGetWindowSize reports.  The wrapper keeps it equal to the VM picture plus the status bar, see below. */
void TsugaruUwpSetWindowSize(int wid,int hei);

/* ---- connection -> host (called from the thread that runs FsSimpleWindowConnection::WindowConnection::Render) ---- */

/* The newest picture of the VM, RGBA8, wid*hei*4 bytes (valid only during the call), plus the status-bar lamp bitmap
   (RGBA8, statusWid*statusHei*4; may be null).  Implemented by the host. */
void TsugaruUwpPublishFrame(int wid,int hei,const unsigned char *rgba,int statusWid,int statusHei,const unsigned char *status);

#ifdef __cplusplus
}
#endif

#endif
