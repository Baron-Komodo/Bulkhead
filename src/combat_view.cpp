#include "combat_view.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace sea {
namespace {

/// A hard ceiling on live particles. The view is presentation only, so when the buffer is full
/// the newest effects are simply dropped rather than allowed to grow without bound during a
/// heavy exchange.
constexpr size_t kMaxParticles = 3500;

// --- Palette ---------------------------------------------------------------
const Color kFlashCore{255, 240, 200, 255};
const Color kFlashTail{255, 150, 40, 0};
const Color kSparkTail{255, 120, 30, 0};
const Color kFireCore{255, 190, 90, 255};
const Color kFireSmoke{70, 64, 60, 0};
const Color kSmokeLight{150, 150, 155, 190};
const Color kSmokeDark{60, 60, 66, 0};
const Color kDeckDark{52, 48, 46, 0};
const Color kWaterBright{210, 232, 244, 235};
const Color kWaterFade{150, 190, 215, 0};
const Color kShockStart{255, 255, 255, 200};
const Color kShockEnd{255, 255, 255, 0};

/// A weapon's size factor: how much flash, water and debris it throws, and how long it lives.
float categoryScale(FireCategory category) {
  switch (category) {
    case FireCategory::light: return 1.0f;
    case FireCategory::medium: return 1.7f;
    case FireCategory::heavy: return 2.8f;
  }
  return 1.0f;
}

/// The screen-space direction a heading points in. Headings are degrees clockwise from north,
/// and north is -y on the map, so 0 is up and 90 is to the right.
Vector2 screenDir(float headingDeg) {
  const float radians = headingDeg * PI / 180.0f;
  return Vector2{std::sin(radians), -std::cos(radians)};
}

/// How long a hull sprite is drawn, matched to the map's own hull sizing so a wreck is the same
/// ship that was afloat a moment ago.
float hullLength(float scale) { return scale > 8.0f ? 34.0f : 24.0f; }

/// A straight per-channel blend from one colour to another, so the view needs no colour maths
/// of its own beyond lerping between a particle's start and end tints.
Color mixColor(Color from, Color to, float t) {
  const auto channel = [t](unsigned char a, unsigned char b) {
    return static_cast<unsigned char>(a + static_cast<float>(b - a) * t);
  };
  return Color{channel(from.r, to.r), channel(from.g, to.g), channel(from.b, to.b),
               channel(from.a, to.a)};
}

}  // namespace

void CombatView::reset() {
  particles_.clear();
  wrecks_.clear();
  smokeTimers_.clear();
  trailTimers_.clear();
}

float CombatView::random(float low, float high) {
  std::uniform_real_distribution<float> dist(low, high);
  return dist(rng_);
}

void CombatView::add(const Particle& particle) {
  if (particles_.size() >= kMaxParticles) return;
  particles_.push_back(particle);
}

// Increased base particle sizes for better visibility
static const float kBaseParticleSizeScale = 1.3f;

void CombatView::update(float deltaSeconds, const std::vector<CombatEvent>& events,
                        const GameState& game) {
  const float dt = std::max(0.0f, deltaSeconds);
  if (dt > 0.0f) {
    for (Particle& particle : particles_) {
      particle.age += dt;
      particle.offset.x += particle.velocity.x * dt;
      particle.offset.y += particle.velocity.y * dt;

      // `drag` is the share of velocity kept per second, so raising it to the frame time keeps
      // the decay frame-rate independent.
      const float retain = std::pow(particle.drag, dt);
      particle.velocity.x *= retain;
      particle.velocity.y *= retain;

      // Smoke is buoyant: it keeps climbing as it thins instead of coasting to a stop.
      if (particle.form == Form::Smoke) particle.velocity.y -= 14.0f * dt;

      particle.size = std::max(0.0f, particle.size + particle.growth * dt);
    }

    particles_.erase(std::remove_if(particles_.begin(), particles_.end(),
                                    [](const Particle& particle) {
                                      return particle.age >= particle.life;
                                    }),
                     particles_.end());

    for (Wreck& wreck : wrecks_) wreck.age += dt;
    wrecks_.erase(std::remove_if(wrecks_.begin(), wrecks_.end(),
                                 [](const Wreck& wreck) { return wreck.age >= wreck.life; }),
                  wrecks_.end());
  }

  // This frame's fight, replayed as effects.
  for (const CombatEvent& event : events) {
    switch (event.kind) {
      case CombatEvent::Kind::Muzzle:
        emitMuzzle(event.at, event.category, event.delivery, static_cast<float>(event.headingDeg),
                   event.mountIndex);
        break;

      case CombatEvent::Kind::Impact:
        emitImpact(event.at, event.category, event.delivery, event.onWater);
        break;

      case CombatEvent::Kind::Kill: {
        // A hull going down: keep it on the surface for a few seconds and take it under, and
        // throw a larger fireball than an ordinary hit.
        Wreck wreck;
        wreck.position = event.at;
        wreck.kind = event.hullKind;
        wreck.headingDeg = event.headingDeg;
        wreck.age = 0.0f;
        wreck.life = 7.0f;
        wrecks_.push_back(wreck);

        emitImpact(event.at, event.category, event.delivery, /*onWater=*/false);
        const float scale = categoryScale(event.category) * 1.6f;
        for (int i = 0; i < 10; ++i) {
          Particle debris;
          debris.anchor = event.at;
          const float angle = random(0.0f, 2.0f * PI);
          const float speed = random(40.0f, 190.0f) * scale;
          debris.velocity = Vector2{std::cos(angle) * speed, std::sin(angle) * speed - 40.0f};
          debris.drag = 0.32f;
          debris.age = 0.0f;
          debris.life = random(0.6f, 1.4f);
          debris.size = random(1.2f, 3.0f);
          debris.growth = -0.6f;
          debris.from = kDeckDark;
          debris.to = kDeckDark;
          debris.form = Form::Debris;
          debris.additively = false;
          add(debris);
        }
        break;
      }
    }
  }

  // Round trails: a paced emission rather than one per frame, so a rocket leaves a smoke
  // column and a torpedo a wake without filling the particle buffer at high frame rates.
  for (const Projectile& shot : game.projectiles()) emitProjectileTrail(shot, dt);

  // Forget the pacing timers of rounds that have landed, so a long fight does not keep one
  // entry per shot ever fired.
  for (auto it = trailTimers_.begin(); it != trailTimers_.end();) {
    bool live = false;
    for (const Projectile& shot : game.projectiles()) {
      if (shot.id == it->first) {
        live = true;
        break;
      }
    }
    it = live ? std::next(it) : trailTimers_.erase(it);
  }

  // Hurt hulls trail smoke. A timer per hull spaces the puffs, so a light hit wisps and a
  // badly mauled ship pours it out.
  auto smokeForHull = [&](int key, const LatLng& at, float healthFraction) {
    if (healthFraction >= 0.6f) {
      smokeTimers_.erase(key);
      return;
    }
    float& timer = smokeTimers_[key];
    timer -= dt;
    if (timer <= 0.0f) {
      emitHullSmoke(at, healthFraction);
      const float severity = 1.0f - healthFraction;
      timer = 0.55f - 0.35f * severity;
    }
  };

  for (const NavalUnit& unit : game.navalUnits()) {
    if (unit.sunk() || unit.maxHealth <= 0.0f) continue;
    smokeForHull(unit.id, unit.position, unit.health / unit.maxHealth);
  }
  for (const BotBoat& boat : game.botBoats()) {
    if (boat.sunk() || boat.maxHealth <= 0.0f) continue;
    // Bot ids are their own sequence, so offset the key to keep the two sets apart.
    smokeForHull(-(boat.id + 1), boat.position, boat.health / boat.maxHealth);
  }

  // A burning wreck keeps smoking as it goes under.
  for (const Wreck& wreck : wrecks_) {
    if (wreck.age < wreck.life * 0.6f && random(0.0f, 1.0f) < 0.18f) {
      emitHullSmoke(wreck.position, 0.15f);
    }
  }
}

void CombatView::emitMuzzle(const LatLng& at, FireCategory category, Delivery delivery,
                            float headingDeg, int mountIndex) {
  const Vector2 dir = screenDir(headingDeg);
  const float scale = categoryScale(category);
  const Vector2 side{-dir.y, dir.x};
  // Mounts alternate to port and starboard, so a broadside reads as several guns rather than
  // one enormous flash coming out of the middle of the hull.
  const float lateral = (mountIndex % 2 == 0 ? 1.0f : -1.0f) *
                        (2.0f + 2.5f * static_cast<float>(mountIndex % 3));
  const Vector2 muzzle{dir.x * (11.0f + 4.0f * scale) + side.x * lateral,
                       dir.y * (11.0f + 4.0f * scale) + side.y * lateral};

  // A torpedo leaves a tube underwater: no gun flash, only foam and bubbles at the surface.
  if (delivery == Delivery::Torpedo) {
    const int bubbles = 5 + static_cast<int>(4.0f * scale);
    for (int i = 0; i < bubbles; ++i) {
      Particle spray;
      spray.anchor = at;
      spray.offset = Vector2{muzzle.x + random(-4.0f, 4.0f), muzzle.y + random(-4.0f, 4.0f)};
      const float angle = random(0.0f, 2.0f * PI);
      const float speed = random(18.0f, 70.0f);
      spray.velocity = Vector2{std::cos(angle) * speed, std::sin(angle) * speed - 10.0f};
      spray.drag = 0.35f;
      spray.life = random(0.25f, 0.55f);
      spray.size = random(1.2f, 2.8f) * scale;
      spray.from = kWaterBright;
      spray.to = kWaterFade;
      spray.form = Form::Spray;
      spray.additively = false;
      add(spray);
    }
    Particle ring;
    ring.anchor = at;
    ring.offset = muzzle;
    ring.life = 0.35f;
    ring.size = 3.0f + scale;
    ring.growth = 55.0f;
    ring.from = kWaterBright;
    ring.to = kWaterFade;
    ring.form = Form::Shock;
    ring.additively = false;
    add(ring);
    return;
  }

  Particle flash;
  flash.anchor = at;
  flash.offset = muzzle;
  flash.velocity = Vector2{dir.x * random(20.0f, 70.0f), dir.y * random(20.0f, 70.0f)};
  flash.drag = 0.02f;
  flash.life = random(0.05f, 0.10f) * scale;
  flash.size = (3.0f + 2.0f * scale) * random(0.8f, 1.3f) * kBaseParticleSizeScale;
  flash.growth = 26.0f * scale * kBaseParticleSizeScale;
  flash.from = kFlashCore;
  flash.to = kFlashTail;
  flash.form = Form::Flash;
  flash.additively = true;
  add(flash);

  // A rocket or missile motor leaves a thick, fast-growing cloud behind the launcher; a big
  // gun pushes a thin puff forward; a machine gun barely smokes at all.
  const int puffs = delivery == Delivery::Rocket || delivery == Delivery::Missile ? 3
                    : delivery == Delivery::MachineGun                        ? 0
                    : category == FireCategory::light                        ? 0
                                                                             : 1;
  for (int i = 0; i < puffs; ++i) {
    Particle smoke;
    smoke.anchor = at;
    smoke.offset = muzzle;
    const bool backblast = delivery == Delivery::Rocket || delivery == Delivery::Missile;
    smoke.velocity = backblast
                         ? Vector2{-dir.x * random(40.0f, 100.0f),
                                   -dir.y * random(40.0f, 100.0f) - random(4.0f, 12.0f)}
                         : Vector2{dir.x * random(30.0f, 80.0f),
                                   dir.y * random(30.0f, 80.0f) - random(6.0f, 18.0f)};
    smoke.drag = 0.35f;
    smoke.life = random(0.5f, 0.9f) * scale;
    smoke.size = 3.0f + 2.0f * scale;
    smoke.growth = 12.0f * scale;
    smoke.from = kSmokeLight;
    smoke.to = kSmokeDark;
    smoke.form = Form::Smoke;
    smoke.additively = false;
    add(smoke);
  }

  const int sparks = delivery == Delivery::MachineGun
                         ? 1
                         : (category == FireCategory::heavy
                                ? 6
                                : (category == FireCategory::medium ? 3 : 1));
  const float baseAngle = std::atan2(dir.y, dir.x);
  for (int i = 0; i < sparks; ++i) {
    Particle spark;
    spark.anchor = at;
    spark.offset = muzzle;
    const float angle = baseAngle + random(-0.5f, 0.5f);
    const float speed = random(60.0f, 180.0f) * scale;
    spark.velocity = Vector2{std::cos(angle) * speed, std::sin(angle) * speed};
    spark.drag = 0.12f;
    spark.life = random(0.08f, 0.2f);
    spark.size = random(1.0f, 2.2f);
    spark.from = kFlashCore;
    spark.to = kSparkTail;
    spark.form = Form::Flash;
    spark.additively = true;
    add(spark);
  }
}

void CombatView::emitImpact(const LatLng& at, FireCategory category, Delivery delivery,
                            bool onWater) {
  // A torpedo throws a much taller column than a shell, and a rocket or missile leaves a
  // fireball rather than a spark. Scaling the existing effect by delivery keeps one impact
  // path while letting each weapon read differently.
  const float scale = categoryScale(category) *
                      (delivery == Delivery::Torpedo
                           ? 1.7f
                           : (delivery == Delivery::Rocket || delivery == Delivery::Missile
                                  ? 1.4f
                                  : 1.0f));

  // Every impact throws a shock ring, whichever element it landed in.
  Particle shock;
  shock.anchor = at;
  shock.life = 0.18f + 0.12f * scale;
  shock.size = 4.0f + 3.0f * scale;
  shock.growth = 90.0f + 60.0f * scale;
  shock.from = kShockStart;
  shock.to = kShockEnd;
  shock.form = Form::Shock;
  shock.additively = true;
  add(shock);

  if (onWater) {
    Particle column;
    column.anchor = at;
    column.velocity = Vector2{0.0f, -random(70.0f, 140.0f) * scale};
    column.drag = 0.45f;
    column.life = random(0.4f, 0.7f) * scale;
    column.size = 2.5f * scale;
    column.growth = 16.0f * scale;
    column.from = kWaterBright;
    column.to = kWaterFade;
    column.form = Form::Splash;
    column.additively = false;
    add(column);

    const int drops = 4 + static_cast<int>(6.0f * scale);
    for (int i = 0; i < drops; ++i) {
      Particle spray;
      spray.anchor = at;
      const float angle = random(0.0f, 2.0f * PI);
      const float speed = random(30.0f, 150.0f) * scale;
      spray.velocity = Vector2{std::cos(angle) * speed, std::sin(angle) * speed - 40.0f};
      if (delivery == Delivery::Torpedo) spray.velocity.y -= 60.0f;
      spray.drag = 0.25f;
      spray.life = random(0.3f, 0.7f) * scale;
      spray.size = random(1.0f, 2.4f);
      spray.growth = -0.8f;
      spray.from = kWaterBright;
      spray.to = kWaterFade;
      spray.form = Form::Spray;
      spray.additively = false;
      add(spray);
    }
    return;
  }

  // A hit on a deck: fireball, then smoke and sparks.
  Particle fire;
  fire.anchor = at;
  fire.velocity = Vector2{random(-12.0f, 12.0f), -random(8.0f, 26.0f)};
  fire.drag = 0.3f;
  fire.life = random(0.22f, 0.42f) * scale;
  fire.size = (5.0f + 4.0f * scale) * random(0.85f, 1.2f);
  fire.growth = 20.0f * scale;
  fire.from = kFireCore;
  fire.to = kFireSmoke;
  fire.form = Form::Fire;
  fire.additively = true;
  add(fire);

  const int puffs = 1 + static_cast<int>(scale);
  for (int i = 0; i < puffs; ++i) {
    Particle smoke;
    smoke.anchor = at;
    smoke.offset = Vector2{random(-6.0f, 6.0f), random(-6.0f, 6.0f)};
    smoke.velocity = Vector2{random(-16.0f, 16.0f), -random(18.0f, 40.0f)};
    smoke.drag = 0.5f;
    smoke.life = random(0.6f, 1.2f) * scale;
    smoke.size = random(4.0f, 8.0f) * scale;
    smoke.growth = 10.0f * scale;
    smoke.from = kSmokeLight;
    smoke.to = kSmokeDark;
    smoke.form = Form::Smoke;
    smoke.additively = false;
    add(smoke);
  }

  const int sparks = 4 + static_cast<int>(5.0f * scale);
  for (int i = 0; i < sparks; ++i) {
    Particle spark;
    spark.anchor = at;
    const float angle = random(0.0f, 2.0f * PI);
    const float speed = random(50.0f, 200.0f) * scale;
    spark.velocity = Vector2{std::cos(angle) * speed, std::sin(angle) * speed};
    spark.drag = 0.14f;
    spark.life = random(0.12f, 0.3f);
    spark.size = random(1.0f, 2.4f);
    spark.from = kFlashCore;
    spark.to = kSparkTail;
    spark.form = Form::Flash;
    spark.additively = true;
    add(spark);
  }
}

void CombatView::emitProjectileTrail(const Projectile& shot, float deltaSeconds) {
  // Pace the emission per round. One particle every frame would turn a rocket into a solid
  // tube of smoke at 144 Hz and a sparse dotted line at 30 Hz; the timer makes the trail look
  // the same whatever the frame rate.
  float interval = 0.03f;
  switch (shot.delivery) {
    case Delivery::MachineGun: interval = 0.05f; break;
    case Delivery::Rocket:
    case Delivery::Missile: interval = 0.025f; break;
    case Delivery::Torpedo: interval = 0.07f; break;
    case Delivery::Gun:
      interval = shot.category == FireCategory::heavy ? 0.02f : 0.035f;
      break;
  }

  const int key = shot.id != 0 ? shot.id : -1;
  float& timer = trailTimers_[key];
  timer -= std::max(0.0f, deltaSeconds);
  if (timer > 0.0f) return;
  timer = interval;

  switch (shot.delivery) {
    case Delivery::Torpedo: {
      // A torpedo runs just under the surface: a churning white wake and a few bubbles.
      Particle wake;
      wake.anchor = shot.position;
      wake.offset = Vector2{random(-3.0f, 3.0f), random(-3.0f, 3.0f)};
      wake.velocity = Vector2{random(-6.0f, 6.0f), random(-4.0f, 4.0f)};
      wake.drag = 0.65f;
      wake.life = random(0.55f, 1.1f);
      wake.size = random(2.0f, 4.4f);
      wake.growth = 5.0f;
      wake.from = kWaterBright;
      wake.to = kWaterFade;
      wake.form = Form::Spray;
      wake.additively = false;
      add(wake);

      if (random(0.0f, 1.0f) < 0.35f) {
        Particle bubble;
        bubble.anchor = shot.position;
        bubble.velocity = Vector2{random(-5.0f, 5.0f), random(-9.0f, -3.0f)};
        bubble.drag = 0.5f;
        bubble.life = random(0.3f, 0.7f);
        bubble.size = random(1.0f, 2.2f);
        bubble.from = kWaterBright;
        bubble.to = kWaterFade;
        bubble.form = Form::Spray;
        bubble.additively = false;
        add(bubble);
      }
      break;
    }

    case Delivery::Rocket:
    case Delivery::Missile: {
      // Motor flame at the tail and a puff of exhaust behind it. A rocket is a short, hot,
      // dirty burn; a missile is a cooler motor with a longer white smoke trail.
      const bool missile = shot.delivery == Delivery::Missile;
      Particle flame;
      flame.anchor = shot.position;
      flame.offset = Vector2{random(-2.0f, 2.0f), random(-2.0f, 2.0f)};
      flame.velocity = Vector2{random(-22.0f, 22.0f), random(-12.0f, 22.0f)};
      flame.drag = 0.15f;
      flame.life = missile ? random(0.08f, 0.18f) : random(0.12f, 0.26f);
      flame.size = missile ? random(1.6f, 3.2f) : random(2.4f, 4.6f);
      flame.growth = 5.0f;
      flame.from = kFlashCore;
      flame.to = kSparkTail;
      flame.form = Form::Fire;
      flame.additively = true;
      add(flame);

      Particle smoke;
      smoke.anchor = shot.position;
      smoke.offset = Vector2{random(-3.0f, 3.0f), random(-3.0f, 3.0f)};
      smoke.velocity = Vector2{random(-16.0f, 16.0f), random(-10.0f, 16.0f)};
      smoke.drag = 0.55f;
      smoke.life = missile ? random(1.0f, 1.9f) : random(0.6f, 1.2f);
      smoke.size = missile ? random(3.0f, 5.2f) : random(2.2f, 4.0f);
      smoke.growth = missile ? 9.0f : 7.0f;
      smoke.from = kSmokeLight;
      smoke.to = kSmokeDark;
      smoke.form = Form::Smoke;
      smoke.additively = false;
      add(smoke);
      break;
    }

    case Delivery::MachineGun: {
      Particle tracer;
      tracer.anchor = shot.position;
      tracer.velocity = Vector2{random(-8.0f, 8.0f), random(-8.0f, 8.0f)};
      tracer.drag = 0.1f;
      tracer.life = random(0.05f, 0.10f);
      tracer.size = random(0.7f, 1.3f);
      tracer.from = kFlashCore;
      tracer.to = kSparkTail;
      tracer.form = Form::Flash;
      tracer.additively = true;
      add(tracer);
      break;
    }

    case Delivery::Gun:
    default: {
      Particle tracer;
      tracer.anchor = shot.position;
      tracer.velocity = Vector2{random(-12.0f, 12.0f), random(-12.0f, 12.0f)};
      tracer.drag = 0.08f;
      tracer.life = random(0.08f, 0.16f);
      tracer.size = shot.category == FireCategory::medium ? random(1.2f, 2.2f)
                                                          : random(0.8f, 1.6f);
      tracer.from = kFlashCore;
      tracer.to = kSparkTail;
      tracer.form = Form::Flash;
      tracer.additively = true;
      add(tracer);
      break;
    }
  }
}

void CombatView::emitHullSmoke(const LatLng& at, float damageFraction) {
  const float severity = 1.0f - std::clamp(damageFraction, 0.0f, 1.0f);
  const int puffs = 1 + static_cast<int>(severity * 2.0f);
  for (int i = 0; i < puffs; ++i) {
    Particle smoke;
    smoke.anchor = at;
    smoke.offset = Vector2{random(-6.0f, 6.0f), random(-8.0f, -2.0f)};
    smoke.velocity = Vector2{random(-14.0f, 14.0f), -random(16.0f, 34.0f) - severity * 20.0f};
    smoke.drag = 0.55f;
    smoke.life = random(0.8f, 1.6f) * (0.8f + severity * 0.6f);
    smoke.size = random(4.0f, 7.0f);
    smoke.growth = 8.0f + severity * 10.0f;
    const unsigned char shade = static_cast<unsigned char>(40.0f + 45.0f * severity);
    smoke.from = Color{shade, shade, static_cast<unsigned char>(shade + 4), 200};
    smoke.to = Color{shade, shade, shade, 0};
    smoke.form = Form::Smoke;
    smoke.additively = false;
    add(smoke);
  }
}

void CombatView::draw(const MapView& map, const NavalView& naval, Rectangle viewport,
                      const GameState& game) const {
  const Rectangle vp = viewport;
  const float pad = 80.0f;

  auto onScreen = [&](Vector2 point) {
    return point.x >= vp.x - pad && point.x <= vp.x + vp.width + pad &&
           point.y >= vp.y - pad && point.y <= vp.y + vp.height + pad;
  };

  // --- Wrecks ---------------------------------------------------------------
  // Drawn first, under everything: a hull going down should read as submerged beneath the
  // fighting above it.
  for (const Wreck& wreck : wrecks_) {
    Vector2 screen = map.project(wreck.position);
    const float t = std::clamp(wreck.age / wreck.life, 0.0f, 1.0f);
    if (!onScreen(screen)) continue;

    const float length = hullLength(map.scale());
    screen.y += t * 12.0f;  // slide under
    const unsigned char base = static_cast<unsigned char>(80.0f + 90.0f * (1.0f - t));
    const Color tint{base, base, static_cast<unsigned char>(base + 6),
                     static_cast<unsigned char>(255.0f * (1.0f - t * 0.85f))};

    if (naval.ready(wreck.kind)) {
      naval.drawHull(wreck.kind, wreck.headingDeg, screen, length, tint);
    } else {
      const float angle = static_cast<float>((wreck.headingDeg - 90.0) * PI / 180.0);
      const float size = length * 0.4f;
      const Vector2 nose{screen.x + std::cos(angle) * size, screen.y + std::sin(angle) * size};
      const Vector2 left{screen.x + std::cos(angle + 2.4f) * size,
                         screen.y + std::sin(angle + 2.4f) * size};
      const Vector2 right{screen.x + std::cos(angle - 2.4f) * size,
                          screen.y + std::sin(angle - 2.4f) * size};
      DrawTriangle(nose, right, left, tint);
    }
  }

  // --- Rounds in flight -----------------------------------------------------
  for (const Projectile& shot : game.projectiles()) {
    const Vector2 head = map.project(shot.position);
    if (!onScreen(head)) continue;

    const Vector2 aim = map.project(shot.target);
    Vector2 dir{aim.x - head.x, aim.y - head.y};
    const float length = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    if (length > 0.001f) {
      dir.x /= length;
      dir.y /= length;
    } else {
      dir = Vector2{0.0f, -1.0f};
    }

    const Vector2 side{-dir.y, dir.x};

    switch (shot.delivery) {
      case Delivery::Torpedo: {
        // A thin white wake trails the body, which sits low in the water as a dark sliver.
        const Vector2 back{head.x - dir.x * 30.0f, head.y - dir.y * 30.0f};
        DrawLineEx(back, head, 2.6f, Color{190, 226, 242, 120});
        DrawLineEx({back.x - side.x * 2.0f, back.y - side.y * 2.0f},
                   {head.x - side.x * 2.0f, head.y - side.y * 2.0f}, 1.0f,
                   Color{225, 242, 250, 180});
        DrawLineEx({back.x + side.x * 2.0f, back.y + side.y * 2.0f},
                   {head.x + side.x * 2.0f, head.y + side.y * 2.0f}, 1.0f,
                   Color{225, 242, 250, 180});
        DrawLineEx({head.x - dir.x * 6.0f, head.y - dir.y * 6.0f}, head, 3.4f,
                   Color{46, 58, 66, 245});
        DrawCircleV(head, 2.2f, Color{26, 32, 38, 255});
        break;
      }

      case Delivery::Rocket:
      case Delivery::Missile: {
        const bool missile = shot.delivery == Delivery::Missile;
        const float tail = missile ? 36.0f : 30.0f;
        const Vector2 back{head.x - dir.x * tail, head.y - dir.y * tail};
        DrawLineEx(back, head, missile ? 5.0f : 4.2f, Color{255, 120, 40, 110});
        DrawLineEx({head.x - dir.x * tail * 0.55f, head.y - dir.y * tail * 0.55f}, head,
                   missile ? 2.8f : 2.4f, Color{255, 190, 100, 230});
        DrawLineEx({head.x - dir.x * 5.0f, head.y - dir.y * 5.0f}, head, 2.0f,
                   Color{255, 248, 220, 255});
        DrawCircleV(head, missile ? 3.4f : 3.0f, Color{255, 236, 190, 250});
        DrawCircleV(head, 1.6f, Color{255, 255, 255, 255});
        break;
      }

      case Delivery::MachineGun: {
        const Vector2 back{head.x - dir.x * 7.0f, head.y - dir.y * 7.0f};
        DrawLineEx(back, head, 1.2f, Color{255, 226, 150, 230});
        DrawCircleV(head, 1.1f, Color{255, 248, 220, 255});
        break;
      }

      case Delivery::Gun:
      default: {
        Color color = kFlashCore;
        float thickness = 1.6f;
        float tail = 9.0f;
        if (shot.category == FireCategory::medium) {
          color = Color{255, 180, 90, 235};
          thickness = 2.2f;
          tail = 14.0f;
        } else if (shot.category == FireCategory::heavy) {
          color = Color{220, 240, 255, 220};
          thickness = 2.6f;
          tail = 18.0f;
        }

        const Vector2 back{head.x - dir.x * tail, head.y - dir.y * tail};
        DrawLineEx(back, head, thickness, color);
        DrawCircleV(head, thickness * 0.9f, color);
        break;
      }
    }
  }

  // --- Particles ------------------------------------------------------------
  // Two passes so the order is right: smoke and water first, then anything that glows, so a
  // fireball always burns through the smoke it is making.
  auto drawParticle = [&](const Particle& particle) {
    const Vector2 anchor = map.project(particle.anchor);
    const Vector2 pos{anchor.x + particle.offset.x, anchor.y + particle.offset.y};
    if (!onScreen(pos)) return;

    const float t = particle.life > 0.0f ? std::clamp(particle.age / particle.life, 0.0f, 1.0f)
                                         : 1.0f;
    const Color color = mixColor(particle.from, particle.to, t);
    if (color.a == 0) return;

    const float radius = std::max(0.0f, particle.size);
    switch (particle.form) {
      case Form::Flash:
      case Form::Fire:
      case Form::Smoke:
      case Form::Splash:
      case Form::Spray:
      case Form::Debris:
        DrawCircleV(pos, radius, color);
        break;
      case Form::Shock: {
        const float inner = std::max(0.0f, radius * 0.82f);
        DrawRing(pos, inner, radius, 0.0f, 360.0f, 32, color);
        break;
      }
    }
  };

  BeginBlendMode(BLEND_ALPHA);
  for (const Particle& particle : particles_) {
    if (!particle.additively) drawParticle(particle);
  }
  BeginBlendMode(BLEND_ADDITIVE);
  for (const Particle& particle : particles_) {
    if (particle.additively) drawParticle(particle);
  }
  EndBlendMode();
}

}  // namespace sea
