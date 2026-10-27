#include "management.h"

#include <algorithm>
#include <cmath>

namespace sea {

namespace {

/// One flag officer the roster is built from. Keeping the table separate makes it cheap to
/// add a card without copying the whole Admiral construction five times.
struct AdmiralSeed {
  int id;
  const char* name;
  const char* surname;
  const char* photo;
  const char* nationality;
  const char* buff;
  const char* bio;
  int buyCost;
  int salary;
};

const AdmiralSeed kRoster[] = {
    {1, "William", "H. McRaven", "asset/admiral/WilliamHMcRaven.jpg", "United States",
     "increase the range of armament",
     "Directed global U.S. special operations and orchestrated the 2011 Navy SEAL raid that "
     "eliminated Osama bin Laden.",
     500, 100},
    {2, "Tony", "Radakin", "asset/admiral/TonyRadakin.jpg", "UK",
     "increase damage against post and coastal structures",
     "Led the Royal Navy's transition to modern carrier strike operations and became the first "
     "naval officer in 20 years to command the entire British Armed Forces.",
     500, 100},
    {3, "Jun", "Dong", "asset/admiral/DongJun.jpg", "China", "reduce time of reparation of ships",
     "Commanded China's Southern Theater, enforcing territorial claims in the South China Sea, "
     "before becoming the country's first naval officer appointed as Minister of Defense.",
     500, 100},
    {4, "Vandier", "Pierre", "asset/admiral/VandierPierre.jpg", "France", "increase the speed of boats",
     "A naval aviator with extensive combat flight experience in Afghanistan and Libya who "
     "modernized France's maritime strategy and currently leads NATO transformation efforts.",
     500, 100},
    {5, "Kumar", "R. Hari", "asset/admiral/KumarRHari.jpg", "India", "increase boats defense",
     "A gunnery specialist who integrated advanced BrahMos missile systems into the fleet and "
     "drove the Indian Navy's push toward domestic warship manufacturing.",
     500, 100},


    {6, "Hammond", "Mark", "asset/admiral/MarkHammond.jpg", "Australia", "increase torpedo reload time",
    " A career submariner who commanded Australia's submarine forces and is currently leading the Royal Australian Navy "
     "'s transition to nuclear-powered submarines under the AUKUS pact.",
     500, 100},

    {7, "Yevmenov", "Nikolai", "asset/admiral/NikolaiYevmenov.png", "Russia", "increase magazine of each arememnt",
    " Commanded Russia's Northern Fleet, focusing heavily on expanding Arctic military infrastructure and deploying new classes of nuclear ballistic missile submarines.",
     500, 100},

    {8, "Credendino", "Enrico", "asset/admiral/enrico_credendino.jpg", "Italy", "speed boost to destroyers, frigates and patrol vessels",
    "Commanded Operation Sophia, the European Union's primary naval mission to disrupt human smuggling networks and enforce arms embargoes in the Mediterranean Sea. ",
     500, 100},
};

}  // namespace

AdmiralBuff Admiral::buffKind() const {
  switch (id) {
    case 1:
      return AdmiralBuff::Range;
    case 2:
      return AdmiralBuff::Damage;
    case 3:
      return AdmiralBuff::Repair;
    case 4:
      return AdmiralBuff::Speed;
    case 5:
      return AdmiralBuff::Defense;
    case 6:
      return AdmiralBuff::TorpedoSpeed;
    case 7:
      return AdmiralBuff::Magazine;
    case 8:
      return AdmiralBuff::SpeedDestroyerFrigatePatrol;
    default:
      return AdmiralBuff::Damage;
  }
}

float Admiral::bonus() const {
  const int rank = std::max(1, level);
  return 0.10f + 0.03f * static_cast<float>(rank - 1);
}

bool Fleet::contains(int unitId) const {
  return std::find(unitIds.begin(), unitIds.end(), unitId) != unitIds.end();
}

FleetLimits fleetLimits(const Research& research) {
  FleetLimits limits;
  limits.maxFleets =
      kBaseFleetLimit + techLevel(research, kFleetCommandTech);
  limits.maxShipsPerFleet =
      kBaseShipsPerFleet + 2 * techLevel(research, kFleetOrganizationTech);
  limits.maxAdmirals =
      kBaseAdmiralLimit + techLevel(research, kAdmiralCorpsTech);
  return limits;
}

std::vector<Admiral> generateAllAdmirals() {
  std::vector<Admiral> roster;
  roster.reserve(sizeof(kRoster) / sizeof(kRoster[0]));
  for (const AdmiralSeed& seed : kRoster) {
    Admiral admiral;
    admiral.id = seed.id;
    admiral.name = seed.name;
    admiral.surname = seed.surname;
    admiral.pathPhoto = seed.photo;
    admiral.nationality = seed.nationality;
    admiral.descriptionBuff = seed.buff;
    admiral.generalDescription = seed.bio;
    admiral.buyCost = seed.buyCost;
    admiral.salary = seed.salary;
    roster.push_back(std::move(admiral));
  }
  return roster;
}

std::string admiralBuffLabel(AdmiralBuff buff) {
  switch (buff) {
    case AdmiralBuff::Range:
      return "Weapon range";
    case AdmiralBuff::Damage:
      return "Weapon damage";
    case AdmiralBuff::Repair:
      return "Repair speed";
    case AdmiralBuff::Speed:
      return "Fleet speed";
    case AdmiralBuff::Defense:
      return "Hull defense";
  }
  return "Fleet bonus";
}

}  // namespace sea
