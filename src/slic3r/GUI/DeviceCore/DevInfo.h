#pragma once
#include <nlohmann/json.hpp>
#include "slic3r/Utils/json_diff.hpp"
#include <wx/string.h>

namespace Slic3r {

class MachineObject;

/* some static info of machine*/ /*TODO*/
class DevInfo
{
public:
    DevInfo(MachineObject* obj) {};
};

} // namespace Slic3r