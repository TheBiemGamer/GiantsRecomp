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
