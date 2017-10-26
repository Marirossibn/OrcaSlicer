#ifndef slic3r_Utils_hpp_
#define slic3r_Utils_hpp_

#include <locale>

namespace Slic3r {

extern void set_logging_level(unsigned int level);
extern void trace(unsigned int level, const char *message);

// Set a path with GUI resource files.
void set_var_dir(const std::string &path);
// Return a full path to the GUI resource files.
const std::string& var_dir();
// Return a full resource path for a file_name.
std::string var(const std::string &file_name);

// Set a path with preset files.
void set_data_dir(const std::string &path);
// Return a full path to the GUI resource files.
const std::string& data_dir();
// Return a full path to a configuration file given its file name..
std::string config_path(const std::string &file_name);
// Return a full path to a configuration file given the section and name.
// The suffix ".ini" will be added if it is missing in the name.
std::string config_path(const std::string &section, const std::string &name);

extern std::locale locale_utf8;
extern std::string encode_path(const char *src);
extern std::string decode_path(const char *src);
extern std::string normalize_utf8_nfc(const char *src);

// Compute the next highest power of 2 of 32-bit v
// http://graphics.stanford.edu/~seander/bithacks.html
template<typename T>
inline T next_highest_power_of_2(T v)
{
	if (v != 0)
	    -- v;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    if (sizeof(T) >= sizeof(uint16_t))
    	v |= v >> 8;
    if (sizeof(T) >= sizeof(uint32_t))
	    v |= v >> 16;
    if (sizeof(T) >= sizeof(uint64_t))
	    v |= v >> 32;
    return ++ v;
}

} // namespace Slic3r

#endif // slic3r_Utils_hpp_
