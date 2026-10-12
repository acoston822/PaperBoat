#pragma once

#include <functional>
#include <string>

#if defined(__IOS__)
namespace Paperboat {
// Shows the system Files picker. onPicked gets the path of a private copy of the chosen file (inside the
// app's temporary directory), or an empty string if the user cancelled or the file could not be read.
void IosPickFile(std::function<void(std::string)> onPicked);
} // namespace Paperboat
#endif
