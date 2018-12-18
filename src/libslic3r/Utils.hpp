#ifndef slic3r_Utils_hpp_
#define slic3r_Utils_hpp_

#include <locale>
#include <utility>
#include <functional>
#include <type_traits>

#include "libslic3r.h"

namespace Slic3r {

extern void set_logging_level(unsigned int level);
extern void trace(unsigned int level, const char *message);
// Format memory allocated, separate thousands by comma.
extern std::string format_memsize_MB(size_t n);
// Return string to be added to the boost::log output to inform about the current process memory allocation.
// The string is non-empty only if the loglevel >= info (3).
extern std::string log_memory_info();
extern void disable_multi_threading();

// Set a path with GUI resource files.
void set_var_dir(const std::string &path);
// Return a full path to the GUI resource files.
const std::string& var_dir();
// Return a full resource path for a file_name.
std::string var(const std::string &file_name);

// Set a path with various static definition data (for example the initial config bundles).
void set_resources_dir(const std::string &path);
// Return a full path to the resources directory.
const std::string& resources_dir();

// Set a path with GUI localization files.
void set_local_dir(const std::string &path);
// Return a full path to the localization directory.
const std::string& localization_dir();

// Set a path with preset files.
void set_data_dir(const std::string &path);
// Return a full path to the GUI resource files.
const std::string& data_dir();

// A special type for strings encoded in the local Windows 8-bit code page.
// This type is only needed for Perl bindings to relay to Perl that the string is raw, not UTF-8 encoded.
typedef std::string local_encoded_string;

// Convert an UTF-8 encoded string into local coding.
// On Windows, the UTF-8 string is converted to a local 8-bit code page.
// On OSX and Linux, this function does no conversion and returns a copy of the source string.
extern local_encoded_string encode_path(const char *src);
extern std::string decode_path(const char *src);
extern std::string normalize_utf8_nfc(const char *src);

// Safely rename a file even if the target exists.
// On Windows, the file explorer (or anti-virus or whatever else) often locks the file
// for a short while, so the file may not be movable. Retry while we see recoverable errors.
extern int rename_file(const std::string &from, const std::string &to);

// Copy a file, adjust the access attributes, so that the target is writable.
extern int copy_file(const std::string &from, const std::string &to);

// File path / name / extension splitting utilities, working with UTF-8,
// to be published to Perl.
namespace PerlUtils {
    // Get a file name including the extension.
    extern std::string path_to_filename(const char *src);
    // Get a file name without the extension.
    extern std::string path_to_stem(const char *src);
    // Get just the extension.
    extern std::string path_to_extension(const char *src);
    // Get a directory without the trailing slash.
    extern std::string path_to_parent_path(const char *src);
};

std::string string_printf(const char *format, ...);

// Timestamp formatted for header_slic3r_generated().
extern std::string timestamp_str();
// Standard "generated by Slic3r version xxx timestamp xxx" header string, 
// to be placed at the top of Slic3r generated files.
inline std::string header_slic3r_generated() { return std::string("generated by " SLIC3R_FORK_NAME " " SLIC3R_VERSION " " ) + timestamp_str(); }

// getpid platform wrapper
extern unsigned get_current_pid();

template <typename Real>
Real round_nearest(Real value, unsigned int decimals)
{
    Real res = (Real)0;
    if (decimals == 0)
        res = ::round(value);
    else
    {
        Real power = ::pow((Real)10, (int)decimals);
        res = ::round(value * power + (Real)0.5) / power;
    }
    return res;
}

// Compute the next highest power of 2 of 32-bit v
// http://graphics.stanford.edu/~seander/bithacks.html
inline uint16_t next_highest_power_of_2(uint16_t v)
{
    if (v != 0)
        -- v;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    return ++ v;
}
inline uint32_t next_highest_power_of_2(uint32_t v)
{
    if (v != 0)
        -- v;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return ++ v;
}
inline uint64_t next_highest_power_of_2(uint64_t v)
{
    if (v != 0)
        -- v;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v |= v >> 32;
    return ++ v;
}

// On some implementations (such as some versions of clang), the size_t is a type of its own, so we need to overload for size_t.
// Typically, though, the size_t type aliases to uint64_t / uint32_t.
// We distinguish that here and provide implementation for size_t if and only if it is a distinct type
template<class T> size_t next_highest_power_of_2(T v,
    typename std::enable_if<std::is_same<T, size_t>::value, T>::type = 0,     // T is size_t
    typename std::enable_if<!std::is_same<T, uint64_t>::value, T>::type = 0,  // T is not uint64_t
    typename std::enable_if<!std::is_same<T, uint32_t>::value, T>::type = 0,  // T is not uint32_t
    typename std::enable_if<sizeof(T) == 8, T>::type = 0)                     // T is 64 bits
{
    return next_highest_power_of_2(uint64_t(v));
}
template<class T> size_t next_highest_power_of_2(T v,
    typename std::enable_if<std::is_same<T, size_t>::value, T>::type = 0,     // T is size_t
    typename std::enable_if<!std::is_same<T, uint64_t>::value, T>::type = 0,  // T is not uint64_t
    typename std::enable_if<!std::is_same<T, uint32_t>::value, T>::type = 0,  // T is not uint32_t
    typename std::enable_if<sizeof(T) == 4, T>::type = 0)                     // T is 32 bits
{
    return next_highest_power_of_2(uint32_t(v));
}


extern std::string xml_escape(std::string text);


class ScopeGuard
{
public:
    typedef std::function<void()> Closure;
private:
    bool committed;
    Closure closure;

public:
    ScopeGuard() {}
    ScopeGuard(Closure closure) : closure(std::move(closure)) {}
    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard(ScopeGuard &&other) : closure(std::move(other.closure)) {}

    ~ScopeGuard()
    {
        if (closure) { closure(); }
    }

    ScopeGuard& operator=(const ScopeGuard&) = delete;
    ScopeGuard& operator=(ScopeGuard &&other)
    {
        closure = std::move(other.closure);
        return *this;
    }

    void reset() { closure = Closure(); }
};

} // namespace Slic3r

#if WIN32
    #define SLIC3R_STDVEC_MEMSIZE(NAME, TYPE) NAME.capacity() * ((sizeof(TYPE) + __alignof(TYPE) - 1) / __alignof(TYPE)) * __alignof(TYPE)
#else
    #define SLIC3R_STDVEC_MEMSIZE(NAME, TYPE) NAME.capacity() * ((sizeof(TYPE) + alignof(TYPE) - 1) / alignof(TYPE)) * alignof(TYPE)
#endif

#endif // slic3r_Utils_hpp_
