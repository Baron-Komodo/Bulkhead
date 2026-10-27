#include <vector>
#include <string>
#include <unordered_map>

struct member {
  int id;
  std::string name;
  std::string surname;
  std::string nationality;

};

class Governement{
  member governementLeader;
  member economyMinistry;
  member armyMinistry;

  std::unordered_map<std::string, int> personnality_traits;
  
};
