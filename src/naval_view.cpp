#include "naval_view.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "data.h"      // resolveDataFile
#include "raymath.h"  // MatrixIdentity

namespace sea {
namespace {

constexpr int kFrameCount = 24;   // 15 degree steps around the compass
constexpr int kFrameSize = 176;   // render target side, pixels
constexpr float kNormalLength = 0.6f;  // the hull's longest horizontal extent, world units

// The camera every hull is baked from: a fixed three-quarter aerial view, high enough to read
// the deck and deckside but tilted enough that the superstructure stands up.
constexpr Vector3 kCameraPosition = {0.0f, 1.15f, 1.55f};
constexpr float kCameraFov = 28.0f;

// How many pixels one world unit occupies at the camera's focal plane. A hull is normalised to
// kNormalLength world units but only fills part of the square frame, so the map cannot scale
// the sprite by the frame width: it needs the hull's own on-screen length, which is this.
const float kPixelsPerUnit = [] {
  const float distance = std::sqrt(kCameraPosition.x * kCameraPosition.x +
                                   kCameraPosition.y * kCameraPosition.y +
                                   kCameraPosition.z * kCameraPosition.z);
  return (kFrameSize * 0.5f) / (distance * std::tan(kCameraFov * 0.5f * PI / 180.0f));
}();
const float kHullPixels = kNormalLength * kPixelsPerUnit;

struct Spec {
  NavalModelKind kind;
  const char* file;
  // The yaw, in degrees, that turns this model's bow to face north (up-screen) at heading
  // zero. These four all model the bow along +Z; the field is per-model so a replacement hull
  // exported on another axis can be corrected without touching the baking code.
  float bowYawDeg;
};

const Spec kSpecs[] = {
    {NavalModelKind::other, "models/other.glb", 500.0f},
    {NavalModelKind::aircraft_carrier, "models/aircraft_carrrier.glb", 500.0f},
    {NavalModelKind::submarine, "models/submarine.glb", 500.0f},
    {NavalModelKind::corvette, "models/corvette.glb", 500.0f},
    {NavalModelKind::cruiser, "models/cruiser.glb", 500.0f},
    {NavalModelKind::destroyer, "models/destroyer.glb", 500.0f},
    {NavalModelKind::frigate, "models/frigate.glb", 500.0f},
    {NavalModelKind::patrol, "models/patrol.glb", 500.0f},

};

bool matrixIsIdentity(const Matrix& m) {
  const Matrix identity = MatrixIdentity();
  const float* a = &m.m0;
  const float* b = &identity.m0;
  for (int i = 0; i < 16; ++i) {
    if (std::fabs(a[i] - b[i]) > 1e-6f) return false;
  }
  return true;
}

/// Applies a model's own transform to its mesh data and clears it. raylib multiplies that
/// transform in *after* the draw call's scale, rotation and translation, so leaving it in
/// place mis-centres and mis-scales any model whose scene root node is offset or rotated -
/// which is exactly what a glTF export with a translated root node produces.
void bakeModelTransform(Model& model) {
  const Matrix m = model.transform;
  if (matrixIsIdentity(m)) return;

  const bool flipsWinding = (m.m0 * m.m5 - m.m1 * m.m4) < 0.0f;

  for (int i = 0; i < model.meshCount; ++i) {
    Mesh& mesh = model.meshes[i];
    if (mesh.vertices == nullptr) continue;

    for (int v = 0; v < mesh.vertexCount; ++v) {
      const Vector3 point{mesh.vertices[v * 3 + 0], mesh.vertices[v * 3 + 1],
                          mesh.vertices[v * 3 + 2]};
      const Vector3 moved = Vector3Transform(point, m);
      mesh.vertices[v * 3 + 0] = moved.x;
      mesh.vertices[v * 3 + 1] = moved.y;
      mesh.vertices[v * 3 + 2] = moved.z;
    }

    // Normals take the linear part only; a mirroring transform would also need the winding
    // flipped, but the hull models are all rigid.
    if (mesh.normals != nullptr && !flipsWinding) {
      for (int v = 0; v < mesh.vertexCount; ++v) {
        const Vector3 normal{mesh.normals[v * 3 + 0], mesh.normals[v * 3 + 1],
                             mesh.normals[v * 3 + 2]};
        const Vector3 moved{m.m0 * normal.x + m.m4 * normal.y + m.m8 * normal.z,
                            m.m1 * normal.x + m.m5 * normal.y + m.m9 * normal.z,
                            m.m2 * normal.x + m.m6 * normal.y + m.m10 * normal.z};
        mesh.normals[v * 3 + 0] = moved.x;
        mesh.normals[v * 3 + 1] = moved.y;
        mesh.normals[v * 3 + 2] = moved.z;
      }
    }

    UploadMesh(&mesh, false);
  }

  model.transform = MatrixIdentity();
}

}  // namespace

void NavalView::load() {
  for (const Spec& spec : kSpecs) {
    Sheet& sheet = sheets_[static_cast<int>(spec.kind)];
    if (sheet.loaded) continue;

    const std::string path = resolveDataFile(spec.file);
    if (GetFileLength(path.c_str()) <= 0) {
      TraceLog(LOG_WARNING, "NAVAL: model %s is missing", path.c_str());
      continue;
    }

    Model model = LoadModel(path.c_str());
    if (model.meshCount <= 0) {
      UnloadModel(model);
      TraceLog(LOG_WARNING, "NAVAL: %s has no meshes", path.c_str());
      continue;
    }


    // Normalise the hull: centre it on the origin and scale it so its longest horizontal
    // extent is one unit. That way every class bakes at the same size and the map only has to
    // scale the finished sprite by the zoom.
    //
    // Fold the model's own transform into the geometry first, so the bounding box below is
    // already in the same space the draw call's scale and rotation act on.
    bakeModelTransform(model);
    const BoundingBox box = GetModelBoundingBox(model);

    const Vector3 center{(box.min.x + box.max.x) * 0.5f, (box.min.y + box.max.y) * 0.5f,
                         (box.min.z + box.max.z) * 0.5f};
    const float extent =
        std::max({box.max.x - box.min.x, box.max.z - box.min.z, 1e-4f});
    const float fit = kNormalLength / extent;
    const Vector3 origin{-center.x * fit, -center.y * fit, -center.z * fit};

    Camera3D camera{};
    camera.position = kCameraPosition;
    camera.target = {0.0f, 0.0f, 0.0f};
    camera.up = {0.0f, 1.0f, 0.0f};
    camera.fovy = kCameraFov;
    camera.projection = CAMERA_PERSPECTIVE;

    sheet.frames.resize(kFrameCount);
    for (int frame = 0; frame < kFrameCount; ++frame) {
      RenderTexture2D target = LoadRenderTexture(kFrameSize, kFrameSize);
      SetTextureFilter(target.texture, TEXTURE_FILTER_BILINEAR);

      // Frame f is the sprite for heading f * 15 degrees: the bow is turned to face that way
      // by rotating the model about its up axis. Positive rotation about +Y is
      // counter-clockwise seen from above, while headings count clockwise, hence the minus.
      const float heading = static_cast<float>(frame) * (360.0f / kFrameCount);
      const float yaw = spec.bowYawDeg - heading;

      BeginTextureMode(target);
      ClearBackground(BLANK);
      BeginMode3D(camera);
      DrawModelEx(model, origin, {0.0f, 1.0f, 0.0f}, yaw, {fit, fit, fit}, WHITE);
      EndMode3D();
      EndTextureMode();

      sheet.frames[frame] = target;
    }

    UnloadModel(model);
    sheet.loaded = true;
    TraceLog(LOG_INFO, "NAVAL: baked %d heading sprites for %s", kFrameCount, spec.file);
  }
}

void NavalView::unload() {
  for (Sheet& sheet : sheets_) {
    for (RenderTexture2D& frame : sheet.frames) {
      if (frame.id != 0) UnloadRenderTexture(frame);
    }
    sheet.frames.clear();
    sheet.loaded = false;
  }
}

bool NavalView::ready(NavalModelKind kind) const {
  const Sheet& sheet = sheets_[static_cast<int>(kind)];
  return sheet.loaded && !sheet.frames.empty();
}

const RenderTexture2D* NavalView::frameFor(NavalModelKind kind, double headingDeg) const {
  const Sheet& sheet = sheets_[static_cast<int>(kind)];
  if (!sheet.loaded || sheet.frames.empty()) return nullptr;

  double heading = std::fmod(headingDeg, 360.0);
  if (heading < 0.0) heading += 360.0;

  const int step = static_cast<int>(std::lround(heading / (360.0 / kFrameCount)));
  return &sheet.frames[step % kFrameCount];
}

void NavalView::drawHull(NavalModelKind kind, double headingDeg, Vector2 center, float length,
                         Color tint) const {
  const RenderTexture2D* frame = frameFor(kind, headingDeg);
  if (frame == nullptr || frame->texture.id == 0) return;

  const float scale = length / kHullPixels;
  const float width = static_cast<float>(frame->texture.width) * scale;
  const float height = static_cast<float>(frame->texture.height) * scale;

  // Render textures are stored bottom-up, so the source rectangle is flipped vertically or
  // the hull draws upside down.
  const Rectangle source{0.0f, 0.0f, static_cast<float>(frame->texture.width),
                         -static_cast<float>(frame->texture.height)};
  const Rectangle dest{center.x - width * 0.5f, center.y - height * 0.5f, width, height};
  DrawTexturePro(frame->texture, source, dest, {0.0f, 0.0f}, 0.0f, tint);
}

void NavalView::drawPreview(NavalModelKind kind, Rectangle bounds, double spinDeg) const {
  const RenderTexture2D* frame = frameFor(kind, spinDeg);
  if (frame == nullptr || frame->texture.id == 0) return;

  const float side = std::min(bounds.width, bounds.height);
  const float scale = side / static_cast<float>(frame->texture.width);
  const float width = frame->texture.width * scale;
  const float height = frame->texture.height * scale;

  const Rectangle source{0.0f, 0.0f, static_cast<float>(frame->texture.width),
                         -static_cast<float>(frame->texture.height)};
  const Rectangle dest{bounds.x + (bounds.width - width) * 0.5f,
                       bounds.y + (bounds.height - height) * 0.5f, width, height};
  DrawTexturePro(frame->texture, source, dest, {0.0f, 0.0f}, 0.0f, WHITE);
}

}  // namespace sea
