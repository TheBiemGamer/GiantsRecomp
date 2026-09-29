// Widens the camera's horizontal frustum bounds to the real configured aspect ratio.
//
// The game builds every perspective projection matrix through one function
// (BuildPerspectiveProjectionMatrix, found while RE'ing for ultrawide support -- see
// docs/superpowers/ RE notes / config/default.toml's comment above 0x822D0198 for how it was
// identified). Its signature, confirmed from its own math (matches the standard
// glFrustum/D3DXMatrixPerspectiveOffCenter form -- (right+left)/(right-left) and
// (top+bottom)/(top-bottom) center-offset terms are present verbatim):
//
//   BuildPerspectiveProjectionMatrix(double left, double right, double bottom, double top,
//                                    double near, double far, float* out_4x4)
//
// left/right/bottom/top define the frustum at the near plane -- exactly the values that encode
// the camera's field of view and aspect ratio. The caller (ComputeCameraFrustumBounds,
// 0x821984B0) computes these from a fixed/bucketed SD-or-HD resolution assumption, not the
// actual runtime window size, so on anything wider than 16:9 the image is either stretched or
// the extra width is simply never shown.
//
// The fix: intercept the call, and if the real configured aspect ratio is wider than what the
// incoming bounds already represent, widen left/right symmetrically about their existing center
// so the vertical FOV (top/bottom) is preserved exactly -- the standard "Hor+" widescreen-fix
// technique. Never narrows (a 4:3 or already-correct 16:9 window is untouched), so this can only
// show more of the scene, never less.

#include <rex/cvar.h>
#include <rex/graphics/video_mode_util.h>
#include <rex/hook.h>

REXCVAR_DECLARE(std::string, resolution);

REXCVAR_DEFINE_BOOL(ultrawide_ui_fix, true, "UI",
                    "Widen the UI/HUD's 2D screen-space projection to match the configured "
                    "resolution's real aspect ratio, so it scales up uniformly instead of "
                    "stretching non-uniformly. Only ever widens. Requires 'resolution' to be "
                    "set to see the real window size.");

// The UI/HUD system builds its 2D screen-space projection through the same low-level
// BuildOrthographicProjectionMatrix (0x822D0100, adjacent to BuildPerspectiveProjectionMatrix,
// same 7-arg signature, textbook off-center orthographic math -- see config/default.toml's
// comment above that entry). Its caller (0x821910A8, a UI-camera-refresh method reached via
// vtable dispatch, discovered while hooking this) always passes left=0, top=0, right=1280,
// bottom=720 -- confirmed live via a temporary diagnostic hook (ORTHO DIAG log), not guessed:
// a hardcoded 1280x720 reference canvas, completely independent of the real window size. That
// fixed canvas gets mapped onto the real (wider) render target, which is the whole stretch bug.
//
// Fix: same Hor+ technique as the camera, applied to this matrix's left/right bounds when they
// match that exact origin-anchored screen-space shape (left==0, top==0 -- guards against
// mis-firing on some other orthographic use of this shared low-level builder, e.g. a shadow map,
// which wouldn't have this signature). Widening left/right proportionally to the real aspect
// makes the horizontal and vertical stretch factors equal, so the UI scales up uniformly instead
// of being non-uniformly warped -- fixes the visible distortion. It does NOT make the UI native-
// sized with the 3D background showing through the margins (the UI still spans the full window,
// just without distortion); that would need a second, separate fix constraining the actual GPU
// viewport rectangle the UI draws into, not yet found.
REX_EXTERN(__imp__BuildOrthographicProjectionMatrix);
REX_HOOK_RAW(BuildOrthographicProjectionMatrix) {
  if (REXCVAR_GET(ultrawide_ui_fix)) {
    int32_t width = 0;
    int32_t height = 0;
    if (rex::graphics::video_mode_util::TryParseResolutionPreset(REXCVAR_GET(resolution), width,
                                                                  height) &&
        width > 0 && height > 0) {
      const double left = ctx.f1.f64;
      const double right = ctx.f2.f64;
      const double bottom = ctx.f3.f64;
      const double top = ctx.f4.f64;

      // Only the observed origin-anchored screen-space UI shape (left==0, top==0). Leaves any
      // other orthographic use of this shared builder untouched.
      if (left == 0.0 && top == 0.0 && right > 0.0 && bottom > 0.0) {
        const double real_aspect = double(width) / double(height);
        const double new_right = bottom * real_aspect;
        if (new_right > right) {  // only widen, never narrow
          const double center_x = right * 0.5;
          const double new_half_width = new_right * 0.5;
          ctx.f1.f64 = center_x - new_half_width;
          ctx.f2.f64 = center_x + new_half_width;
        }
      }
    }
  }

  __imp__BuildOrthographicProjectionMatrix(ctx, base);
}

REXCVAR_DEFINE_BOOL(ultrawide_fov, true, "Graphics",
                    "Widen the camera's horizontal field of view to match the configured "
                    "resolution's real aspect ratio, instead of the game's built-in 16:9 "
                    "assumption. Only ever widens (a 4:3 or 16:9 resolution is untouched). "
                    "Requires 'resolution' to be set to see the real window size.");

REX_EXTERN(__imp__BuildPerspectiveProjectionMatrix);

REX_HOOK_RAW(BuildPerspectiveProjectionMatrix) {
  if (REXCVAR_GET(ultrawide_fov)) {
    int32_t width = 0;
    int32_t height = 0;
    if (rex::graphics::video_mode_util::TryParseResolutionPreset(REXCVAR_GET(resolution), width,
                                                                  height) &&
        width > 0 && height > 0) {
      const double left = ctx.f1.f64;
      const double right = ctx.f2.f64;
      const double bottom = ctx.f3.f64;
      const double top = ctx.f4.f64;

      const double half_height = (top - bottom) * 0.5;
      const double current_half_width = (right - left) * 0.5;
      if (half_height > 0.0) {
        const double real_aspect = double(width) / double(height);
        const double new_half_width = half_height * real_aspect;
        if (new_half_width > current_half_width) {  // only widen, never narrow
          const double center_x = (left + right) * 0.5;
          ctx.f1.f64 = center_x - new_half_width;
          ctx.f2.f64 = center_x + new_half_width;
        }
      }
    }
  }

  __imp__BuildPerspectiveProjectionMatrix(ctx, base);
}
