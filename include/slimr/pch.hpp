#pragma once

// Common standard library headers
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <expected>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unistd.h>
#include <unordered_map>
#include <variant>
#include <vector>

// External / system libraries
#include <sqlite3.h>
#include <curl/curl.h>

// Slimr modules
#include "slimr/test.hpp"
#include "slimr/json.hpp"
#include "slimr/reflect.hpp"
#include "slimr/yaml.hpp"
#include "slimr/sqlite.hpp"
#include "slimr/curl.hpp"
#include "slimr/ws.hpp"
#include "slimr/bugsink.hpp"
#include "slimr/mcp.hpp"
#include "slimr/encoding.hpp"
#include "slimr/proc.hpp"
#include "slimr/time.hpp"
#include "slimr/text.hpp"
