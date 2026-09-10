#pragma once

#include <fmt/format.h>

#include <corvusoft/restbed/service.hpp>
#include <corvusoft/restbed/session.hpp>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
#include <wbr/string_manipulations.hxx>

#include "../types.hxx"
#if USE_PALSIGSLOT
  #include <sigslot/signal.hpp>
#endif

// Bump this whenever addon_api_i/http_api_i/board_i/cell_i change shape in any way
// that would make an already-built plugin call the wrong vtable slot (reordering,
// removing, or changing the signature of an existing virtual method) - new methods
// appended at the end don't need a bump. See addon_api_abi_tag() below: it folds
// this in alongside compiler/stdlib identity so a mismatched plugin is refused at
// load time instead of corrupting memory once called.
#define ADDON_API_ABI_VERSION 1

// Everything crossing the addon_api_i/http_api_i/board_i boundary - including this
// header itself, since sigslot::signal<> is header-only and its layout is whatever
// each translation unit's compiler/flags produce - only has a well-defined, agreed
// layout when the plugin and the server are built with the same compiler, same
// standard library, and the same version of this header. There is no portable way
// to make a C++ virtual-interface ABI like this one safe across arbitrary
// compilers/standard libraries (that needs a plain-C ABI instead); this tag exists
// only to turn a silent mismatch into a loud, logged refusal to load, rather than a
// memory-corruption crash somewhere unrelated later on.
// static, not inline: this must never become an exported, externally-linked
// symbol. An inline (weak, default-visibility) definition here would be
// resolved via the process's global symbol scope - and an executable's own
// exported symbols always win that resolution for anything it dlopen()s,
// regardless of RTLD_LOCAL on the loaded library. That silently interposes
// the *host's* copy of this function into every plugin's call to it,
// making the whole ABI check compare the host's tag against itself no
// matter what the plugin was actually built with. static (internal
// linkage) gives every translation unit - the host and each plugin - its
// own private, non-exported copy that can't be interposed.
[[nodiscard, maybe_unused]] static std::string addon_api_abi_tag ( ) {
#if defined(__clang__)
  const std::string compiler = fmt::format("clang-{}", __clang_version__);
#elif defined(__GNUC__)
  const std::string compiler = fmt::format("gcc-{}.{}.{}", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#else
  const std::string compiler = "unknown-compiler";
#endif
#if defined(_GLIBCXX_USE_CXX11_ABI)
  constexpr int cxx11_abi = _GLIBCXX_USE_CXX11_ABI;
#else
  constexpr int cxx11_abi = -1;
#endif
  return fmt::format("addon_api_v{}|{}|cxx{}|glibcxx_abi{}", ADDON_API_ABI_VERSION, compiler, __cplusplus, cxx11_abi);
}

// Every plugin must invoke this exactly once, at namespace scope, to export the
// abi_tag() symbol the server checks (via dlsym) before calling init_plugin(). A
// plugin that doesn't export it is refused just like one missing init_plugin.
#define ADDON_PLUGIN_ABI_TAG( )                          \
  extern "C" const char* abi_tag( ) {                    \
    static const std::string tag = addon_api_abi_tag( ); \
    return tag.c_str( );                                 \
  }

template<size_t dimension = 2>
  requires(dimension > 0)
struct m_coord_t {
  m_coord_t ( ) {
    std::fill(vec.begin( ), vec.end( ), -1);
  }

  m_coord_t(const m_coord_t& other)                 = default;
  m_coord_t(m_coord_t&& other) noexcept             = default;
  m_coord_t& operator= (const m_coord_t& other)     = default;
  m_coord_t& operator= (m_coord_t&& other) noexcept = default;

  [[nodiscard]] constexpr bool operator== (const m_coord_t& other) const noexcept {
    return std::ranges::equal(vec, other.vec);
  }

  [[nodiscard]] constexpr std::strong_ordering operator<=> (const m_coord_t& other) const noexcept {
    if ( *this == other ) {
      return std::strong_ordering::equal;
    }
    return std::lexicographical_compare(vec.begin( ), vec.end( ), other.vec.begin( ), other.vec.end( )) ? std::strong_ordering::less : std::strong_ordering::greater;
  }

  template<typename... Args>
  m_coord_t(Args... args)
    requires(sizeof...(args) == dimension) && (std::is_convertible_v<Args, int> && ...)
      : vec {args...} {
  }

  int operator[] (size_t index) const noexcept {
    return vec[index];
  }

  [[nodiscard]] operator bool ( ) const noexcept {
    return std::ranges::all_of(vec, [] (int v) { return v > 0; });
  }

  std::string dim_name (int i) const noexcept {
    switch ( i ) {
      case 0:  return "x";
      case 1:  return "y";
      case 2:  return "z";
      case 3:  return "i";
      case 4:  return "j";
      case 5:  return "k";
      default: return fmt::format("dim{}", i);
    }
  }

  [[nodiscard]] int x ( ) const noexcept
    requires(dimension > 0)
  {
    return vec[0];
  }

  [[nodiscard]] int y ( ) const noexcept
    requires(dimension > 1)
  {
    return vec[1];
  }

  [[nodiscard]] int z ( ) const noexcept
    requires(dimension > 2)
  {
    return vec[2];
  }

  [[nodiscard]] int i ( ) const noexcept
    requires(dimension > 3)
  {
    return vec[3];
  }

  [[nodiscard]] int j ( ) const noexcept
    requires(dimension > 4)
  {
    return vec[4];
  }

  [[nodiscard]] int k ( ) const noexcept
    requires(dimension > 5)
  {
    return vec[5];
  }

  [[nodiscard]] consteval size_t rank ( ) const noexcept {
    return dimension;
  }

  [[nodiscard]]
  std::string to_string ( ) const noexcept {
    return fmt::format("({})", fmt::join(vec, ", "));
  }

private:
  std::array<int, dimension> vec { };
};

using coord_t = m_coord_t<2>;

FMT_BEGIN_NAMESPACE

template<typename T, size_t n>
[[nodiscard]] constexpr T to_string (const m_coord_t<n>& coord) {
  return T {coord.to_string( )};
}

template<size_t dimension>
struct formatter<m_coord_t<dimension>> : formatter<std::string> {
  template<typename FormatContext>
  auto format (const m_coord_t<dimension>& coord, FormatContext& ctx) const {
    if ( !coord ) {
      return formatter<std::string>::format("nullopt", ctx);
    }
    return formatter<std::string>::format(coord.to_string( ), ctx);
  }
};

FMT_END_NAMESPACE

struct cell_i {
  virtual ~cell_i( ) = default;

  [[nodiscard]] virtual bool is_revealed( ) const = 0;
  [[nodiscard]] virtual bool is_flag( ) const     = 0;
  [[nodiscard]] virtual bool is_boom( ) const     = 0;

  virtual void set_revealed( ) = 0;
  virtual void set_boom( )     = 0;

  virtual void toggle_flag( ) = 0;

  [[nodiscard]] virtual int neighbor_bombs_count ( ) const {
    return -1;
  }

  virtual void set_neighbor_bombs_count(int count) = 0;
};

struct reveal_result_t {
  coord_t coord;
  int     count;
};

struct board_i {
  virtual ~board_i( ) = default;

  virtual std::shared_ptr<board_i> clone( ) const = 0;

  [[nodiscard]] virtual std::size_t width( ) const noexcept  = 0;
  [[nodiscard]] virtual std::size_t height( ) const noexcept = 0;

  [[nodiscard]] coord_t coord (int x, int y) const noexcept {
    if ( is_valid_x(x) && is_valid_y(y) )
      return coord_t {x, y};
    return coord_t { };
  }

  [[nodiscard]] virtual cell_i&       cell(const coord_t& coord)       = 0;
  [[nodiscard]] virtual const cell_i& cell(const coord_t& coord) const = 0;

  [[nodiscard]] virtual int bombs_total( ) const      = 0;
  [[nodiscard]] virtual int flags_count( ) const      = 0;
  [[nodiscard]] virtual int bombs_count( ) const      = 0;
  [[nodiscard]] virtual int unrevealed_count( ) const = 0;

  [[nodiscard]] virtual std::vector<coord_t> neighbors(const coord_t& coord) const = 0;

  [[nodiscard]] virtual int neighbor_bombs_count(const coord_t& coord) const      = 0;
  [[nodiscard]] virtual int neighbor_flags_count(const coord_t& coord) const      = 0;
  [[nodiscard]] virtual int neighbor_revealed_count(const coord_t& coord) const   = 0;
  [[nodiscard]] virtual int neighbor_unrevealed_count(const coord_t& coord) const = 0;

  virtual bool none_of_cell(std::function<bool(const cell_i& cell)> func) const = 0;

  virtual int reveal(const coord_t& coord) = 0;

  // Auto-reveal: opening a cell with 0 neighbouring mines recursively opens all
  // of its neighbours (and, transitively, their neighbours), but this flood-fill
  // can never open a mine.
  [[nodiscard]] virtual std::vector<reveal_result_t> reveal_cells(const coord_t& coord) = 0;

protected:
  [[nodiscard]] virtual bool is_valid_x(int x) const noexcept = 0;
  [[nodiscard]] virtual bool is_valid_y(int y) const noexcept = 0;
};

using board_id_t = uint64_t;

using client_id_t = uint64_t;

using SessionPtr  = std::shared_ptr<restbed::Session>;
using ResourcePtr = std::shared_ptr<restbed::Resource>;
using headers_t   = std::multimap<std::string, std::string>;

// A parameter is either a scalar value, an array of parameters of the same
// (recursive) type (arrays of arrays are allowed), or a map of named
// parameters of the same (recursive) type (maps of maps/arrays, and vice
// versa, are allowed too). This is implemented as a variant deriving struct
// so that `std::vector<parameter_t>` / `std::unordered_map<std::string,
// parameter_t>` can appear as alternatives of `parameter_t` itself (allowed
// since C++17 relaxed the incomplete-type requirements for `std::vector`;
// libstdc++'s node-based `std::unordered_map` supports incomplete mapped
// types the same way in practice).
struct parameter_t : std::variant<std::string, int64_t, uint64_t, bool, std::vector<parameter_t>, std::unordered_map<std::string, parameter_t>> {
  using variant::variant;
};

using parameter_list_t = std::vector<parameter_t>;
using parameter_map_t  = std::unordered_map<std::string, parameter_t>;

template<typename T>
using result_t = std::expected<T, std::string>;

struct parameter_bytestream_t {
  parameter_bytestream_t (std::byte* buffer, size_t buffer_size) : buffer_(buffer), buffer_size_(buffer_size) {
  }

  explicit parameter_bytestream_t (std::span<std::byte> buffer) : buffer_(buffer.data( )), buffer_size_(buffer.size( )) {
  }

  // Bump when serialize()/deserialize()'s wire grammar changes shape in a
  // way that would make an old reader misparse a new writer's bytes (new
  // type_tag values appended at the end are fine; anything else isn't).
  // Written/checked once per buffer by store()/load(), outside the
  // recursive grammar itself - serialize()/deserialize() stay unaware it
  // exists.
  static constexpr uint8_t wire_version = 1;

  // Public entry points: store()/load() are the only way in or out of a
  // buffer, and neither ever lets an exception reach the caller - every
  // internal failure (buffer overrun, malformed tag, version mismatch,
  // reuse of an already-used instance) comes back as the error side of
  // result_t, so a caller on the other side of a dlopen boundary can't
  // forget to handle it and doesn't need to know this type can throw at
  // all internally. serialize()/deserialize() are the recursive core and
  // are deliberately private - a caller only ever deals in whole, versioned
  // buffers.
  [[nodiscard]] result_t<void> store (const parameter_t& param) {
    if ( offset_ != 0 ) {
      return std::unexpected(fmt::format("parameter_bytestream_t::store: instance already used (offset {} != 0); use a fresh instance per buffer", offset_));
    }
    try {
      put_byte(wire_version);
      serialize(param);
    } catch ( const std::exception& e ) {
      return std::unexpected(fmt::format("parameter_bytestream_t::store failed: {}", e.what( )));
    }
    return { };
  }

  [[nodiscard]] result_t<parameter_t> load ( ) {
    if ( offset_ != 0 ) {
      return std::unexpected(fmt::format("parameter_bytestream_t::load: instance already used (offset {} != 0); use a fresh instance per buffer", offset_));
    }
    try {
      const uint8_t version = read_byte( );
      if ( version != wire_version ) {
        return std::unexpected(fmt::format("parameter_bytestream_t::load: wire version mismatch (expected {}, got {})", wire_version, version));
      }
      return deserialize( );
    } catch ( const std::exception& e ) {
      return std::unexpected(fmt::format("parameter_bytestream_t::load failed: {}", e.what( )));
    }
  }

private:
  // Internal-only: serialize()/deserialize()'s recursive descent unwinds
  // through this on any failure. It never crosses store()/load() - both
  // catch std::exception and convert to result_t's error side before
  // returning.
  struct error : std::runtime_error {
    using std::runtime_error::runtime_error;
  };

  enum type_tag : uint8_t {
    null,
    string1,
    string2,
    string3,
    string4,
    string5,
    string6,
    string7,
    string8,
    number1,
    number2,
    number3,
    number4,
    number5,
    number6,
    number7,
    number8,
    neg_number1,
    neg_number2,
    neg_number3,
    neg_number4,
    neg_number5,
    neg_number6,
    neg_number7,
    neg_number8,
    boolean,
    vector,
    map,
  };

  std::byte* buffer_;
  size_t     buffer_size_;
  off_t      offset_ {0};

  template<typename T>
  T* as_ptr (std::byte* ptr) {
    return std::bit_cast<T*>(ptr);
  }

  template<typename T>
  T* as_ptr ( ) {
    return as_ptr<T>(buffer_ + offset_);
  }

  template<typename T>
  T& as_ref (std::byte* ptr) {
    return *as_ptr<T>(ptr);
  }

  template<typename T>
  T& as_ref ( ) {
    return *as_ptr<T>(buffer_ + offset_);
  }

  // Every read/write primitive below funnels through here first, so a
  // truncated, corrupted, or maliciously short buffer fails loudly right
  // where it would otherwise read/write out of bounds, instead of silently
  // touching memory past buffer_size_.
  void check_capacity (size_t needed) const {
    if ( static_cast<size_t>(offset_) + needed > buffer_size_ ) {
      throw error(fmt::format("parameter_bytestream_t: buffer overrun at offset {} (need {} more bytes, capacity {})", offset_, needed, buffer_size_));
    }
  }

  void write_tag (type_tag tag) {
    check_capacity(sizeof(type_tag));
    as_ref<type_tag>( ) = tag;
    offset_ += sizeof(type_tag);
  }

  void write_len (size_t len) {
    write(len);
  }

  template<std::convertible_to<std::string> T>
  void write (const T& str) {
    const size_t str_size = str.size( );
    check_capacity(sizeof(type_tag) + sizeof(size_t) + str_size);

    if ( str_size < 0x100 ) {
      write_tag(type_tag::string1);
      put_byte(byte(str_size, 0));
    } else if ( str_size < 0x1'00'00 ) {
      write_tag(type_tag::string2);
      put_byte(byte(str_size, 0));
      put_byte(byte(str_size, 1));
    } else if ( str_size < 0x1'00'00'00 ) {
      write_tag(type_tag::string3);
      put_byte(byte(str_size, 0));
      put_byte(byte(str_size, 1));
      put_byte(byte(str_size, 2));
    } else if ( str_size < 0x1'00'00'00'00 ) {
      write_tag(type_tag::string4);
      put_byte(byte(str_size, 0));
      put_byte(byte(str_size, 1));
      put_byte(byte(str_size, 2));
      put_byte(byte(str_size, 3));
    } else if ( str_size < 0x1'00'00'00'00'00 ) {
      write_tag(type_tag::string5);
      put_byte(byte(str_size, 0));
      put_byte(byte(str_size, 1));
      put_byte(byte(str_size, 2));
      put_byte(byte(str_size, 3));
      put_byte(byte(str_size, 4));
    } else if ( str_size < 0x1'00'00'00'00'00'00 ) {
      write_tag(type_tag::string6);
      put_byte(byte(str_size, 0));
      put_byte(byte(str_size, 1));
      put_byte(byte(str_size, 2));
      put_byte(byte(str_size, 3));
      put_byte(byte(str_size, 4));
      put_byte(byte(str_size, 5));
    } else if ( str_size < 0x1'00'00'00'00'00'00'00 ) {
      write_tag(type_tag::string7);
      put_byte(byte(str_size, 0));
      put_byte(byte(str_size, 1));
      put_byte(byte(str_size, 2));
      put_byte(byte(str_size, 3));
      put_byte(byte(str_size, 4));
      put_byte(byte(str_size, 5));
      put_byte(byte(str_size, 6));
    } else {
      write_tag(type_tag::string8);
      put_byte(byte(str_size, 0));
      put_byte(byte(str_size, 1));
      put_byte(byte(str_size, 2));
      put_byte(byte(str_size, 3));
      put_byte(byte(str_size, 4));
      put_byte(byte(str_size, 5));
      put_byte(byte(str_size, 6));
      put_byte(byte(str_size, 7));
    }

    std::copy_n(str.data( ), str_size, as_ptr<char>( ));
    offset_ += str_size;
  }

  void put_byte (uint8_t byte) {
    check_capacity(sizeof(uint8_t));
    as_ref<uint8_t>( ) = byte;
    offset_ += sizeof(uint8_t);
  }

  [[nodiscard]] uint8_t byte (uint64_t v, int byte_index) {
    return static_cast<uint8_t>((v >> (byte_index * 8)) & 0xFF);
  }

  template<std::integral T>
  void write (T val) {
    check_capacity(sizeof(type_tag) + sizeof(T));
    if ( val >= 0 ) {
      const uint64_t v = static_cast<uint64_t>(val);
      if ( v < 0x1'00 ) {
        write_tag(type_tag::number1);
        put_byte(byte(v, 0));
      } else if ( v < 0x1'00'00 ) {
        write_tag(type_tag::number2);
        put_byte(byte(v, 0));
        put_byte(byte(v, 1));
      } else if ( v < 0x1'00'00'00 ) {
        write_tag(type_tag::number3);
        put_byte(byte(v, 0));
        put_byte(byte(v, 1));
        put_byte(byte(v, 2));
      } else if ( v < 0x1'00'00'00'00 ) {
        write_tag(type_tag::number4);
        put_byte(byte(v, 0));
        put_byte(byte(v, 1));
        put_byte(byte(v, 2));
        put_byte(byte(v, 3));
      } else if ( v < 0x1'00'00'00'00'00 ) {
        write_tag(type_tag::number5);
        put_byte(byte(v, 0));
        put_byte(byte(v, 1));
        put_byte(byte(v, 2));
        put_byte(byte(v, 3));
        put_byte(byte(v, 4));
      } else if ( v < 0x1'00'00'00'00'00'00 ) {
        write_tag(type_tag::number6);
        put_byte(byte(v, 0));
        put_byte(byte(v, 1));
        put_byte(byte(v, 2));
        put_byte(byte(v, 3));
        put_byte(byte(v, 4));
        put_byte(byte(v, 5));
      } else if ( v < 0x1'00'00'00'00'00'00'00 ) {
        write_tag(type_tag::number7);
        put_byte(byte(v, 0));
        put_byte(byte(v, 1));
        put_byte(byte(v, 2));
        put_byte(byte(v, 3));
        put_byte(byte(v, 4));
        put_byte(byte(v, 5));
        put_byte(byte(v, 6));
      } else {
        write_tag(type_tag::number8);
        put_byte(byte(v, 0));
        put_byte(byte(v, 1));
        put_byte(byte(v, 2));
        put_byte(byte(v, 3));
        put_byte(byte(v, 4));
        put_byte(byte(v, 5));
        put_byte(byte(v, 6));
        put_byte(byte(v, 7));
      }
    } else {
      const uint64_t v = -static_cast<uint64_t>(val);
      if ( v < 0x1'00 ) {
        write_tag(type_tag::neg_number1);
        put_byte(byte(v, 0));
      } else if ( v < 0x1'00'00 ) {
        write_tag(type_tag::neg_number2);
        put_byte(byte(v, 0));
        put_byte(byte(v, 1));
      } else if ( v < 0x1'00'00'00 ) {
        write_tag(type_tag::neg_number3);
        put_byte(byte(v, 0));
        put_byte(byte(v, 1));
        put_byte(byte(v, 2));
      } else if ( v < 0x1'00'00'00'00 ) {
        write_tag(type_tag::neg_number4);
        put_byte(byte(v, 0));
        put_byte(byte(v, 1));
        put_byte(byte(v, 2));
        put_byte(byte(v, 3));
      } else if ( v < 0x1'00'00'00'00'00 ) {
        write_tag(type_tag::neg_number5);
        put_byte(byte(v, 0));
        put_byte(byte(v, 1));
        put_byte(byte(v, 2));
        put_byte(byte(v, 3));
        put_byte(byte(v, 4));
      } else if ( v < 0x1'00'00'00'00'00'00 ) {
        write_tag(type_tag::neg_number6);
        put_byte(byte(v, 0));
        put_byte(byte(v, 1));
        put_byte(byte(v, 2));
        put_byte(byte(v, 3));
        put_byte(byte(v, 4));
        put_byte(byte(v, 5));
      } else if ( v < 0x1'00'00'00'00'00'00'00 ) {
        write_tag(type_tag::neg_number7);
        put_byte(byte(v, 0));
        put_byte(byte(v, 1));
        put_byte(byte(v, 2));
        put_byte(byte(v, 3));
        put_byte(byte(v, 4));
        put_byte(byte(v, 5));
        put_byte(byte(v, 6));
      } else {
        write_tag(type_tag::neg_number8);
        put_byte(byte(v, 0));
        put_byte(byte(v, 1));
        put_byte(byte(v, 2));
        put_byte(byte(v, 3));
        put_byte(byte(v, 4));
        put_byte(byte(v, 5));
        put_byte(byte(v, 6));
        put_byte(byte(v, 7));
      }
    }
  }

  void write (bool val) {
    check_capacity(sizeof(type_tag) + sizeof(bool));
    write_tag(type_tag::boolean);
    as_ref<bool>( ) = val;
    offset_ += sizeof(bool);
  }

  void write (const parameter_list_t& vec) {
    write_tag(type_tag::vector);
    write_len(vec.size( ));
    for ( const auto& item: vec ) {
      serialize(item);
    }
  }

  void write (const parameter_map_t& m) {
    write_tag(type_tag::map);
    write_len(m.size( ));
    for ( const auto& [key, item]: m ) {
      write(key);
      serialize(item);
    }
  }

  void serialize (const parameter_t& param) {
    if ( std::holds_alternative<std::string>(param) ) {
      write(std::get<std::string>(param));
    } else if ( std::holds_alternative<int64_t>(param) ) {
      write(std::get<int64_t>(param));
    } else if ( std::holds_alternative<uint64_t>(param) ) {
      write(static_cast<int64_t>(std::get<uint64_t>(param)));
    } else if ( std::holds_alternative<bool>(param) ) {
      write(std::get<bool>(param));
    } else if ( std::holds_alternative<parameter_list_t>(param) ) {
      write(std::get<parameter_list_t>(param));
    } else if ( std::holds_alternative<parameter_map_t>(param) ) {
      write(std::get<parameter_map_t>(param));
    } else {
      throw error("parameter_bytestream_t: parameter_t holds an unserializable alternative");
    }
  }

  size_t read_len ( ) {
    const type_tag tag = read_tag( );
    switch ( tag ) {
      using enum type_tag;
      case number1:     return read_integer(1);
      case number2:     return read_integer(2);
      case number3:     return read_integer(3);
      case number4:     return read_integer(4);
      case number5:     return read_integer(5);
      case number6:     return read_integer(6);
      case number7:     return read_integer(7);
      case number8:     return read_integer(8);
      default:          throw error(fmt::format("parameter_bytestream_t: invalid type tag {} for length", static_cast<int>(tag)));
    }
  }

  type_tag read_tag ( ) {
    check_capacity(sizeof(type_tag));
    const auto ret = as_ref<type_tag>( );
    offset_ += sizeof(type_tag);
    return ret;
  }

  std::string read_string (int bytes) {
    const size_t len = read_integer(bytes);
    check_capacity(len);
    const std::string_view sv {as_ptr<char>( ), len};
    offset_ += len;
    return std::string {sv};
  }

  uint8_t read_byte ( ) {
    check_capacity(sizeof(uint8_t));
    const uint8_t ret = as_ref<uint8_t>( );
    offset_ += sizeof(uint8_t);
    return ret;
  }

  template<std::integral T = int64_t>
  T read_integer (size_t bytes) {
    int64_t ret = 0;
    for ( size_t i = 0; i < bytes; ++i ) {
      ret |= static_cast<int64_t>(read_byte( )) << (i * 8);
    }
    return static_cast<T>(ret);
  }

  bool read_bool ( ) {
    check_capacity(sizeof(bool));
    const bool ret = as_ref<bool>( );
    offset_ += sizeof(bool);
    return ret;
  }

  parameter_list_t read_vector ( ) {
    const size_t     len = read_len( );
    parameter_list_t vec;
    for ( size_t i = 0; i < len; ++i ) {
      vec.push_back(deserialize( ));
    }
    return vec;
  }

  parameter_map_t read_map ( ) {
    const size_t    len = read_len( );
    parameter_map_t m;
    for ( size_t i = 0; i < len; ++i ) {
      const type_tag    tag = read_tag( );
      const std::string key = [this] (type_tag e) {
        switch ( e ) {
          using enum type_tag;
          case string1: return read_string(1);
          case string2: return read_string(2);
          case string3: return read_string(3);
          case string4: return read_string(4);
          case string5: return read_string(5);
          case string6: return read_string(6);
          case string7: return read_string(7);
          case string8: return read_string(8);
          default:      throw error(fmt::format("parameter_bytestream_t: invalid type tag {} for map key", static_cast<int>(e)));
        }
      }(tag);
      m.emplace(key, deserialize( ));
    }
    return m;
  }

  parameter_t deserialize ( ) {
    const type_tag tag = read_tag( );
    switch ( tag ) {
      using enum type_tag;
      case null:        return { };
      case string1:     return read_string(1);
      case string2:     return read_string(2);
      case string3:     return read_string(3);
      case string4:     return read_string(4);
      case string5:     return read_string(5);
      case string6:     return read_string(6);
      case string7:     return read_string(7);
      case string8:     return read_string(8);
      case number1:     return read_integer(1);
      case number2:     return read_integer(2);
      case number3:     return read_integer(3);
      case number4:     return read_integer(4);
      case number5:     return read_integer(5);
      case number6:     return read_integer(6);
      case number7:     return read_integer(7);
      case number8:     return read_integer(8);
      case neg_number1: return -read_integer(1);
      case neg_number2: return -read_integer(2);
      case neg_number3: return -read_integer(3);
      case neg_number4: return -read_integer(4);
      case neg_number5: return -read_integer(5);
      case neg_number6: return -read_integer(6);
      case neg_number7: return -read_integer(7);
      case neg_number8: return static_cast<int64_t>(-read_integer<uint64_t>(8));
      case boolean:     return read_bool( );
      case vector:      return read_vector( );
      case map:         return read_map( );
    }
    throw error(fmt::format("parameter_bytestream_t: invalid type tag {}", static_cast<int>(tag)));
  }
};

class rest_api_response_i {
public:
  virtual ~rest_api_response_i( ) = default;

  virtual rest_api_response_i& add_property(const std::string& key, parameter_t value) = 0;

  rest_api_response_i& add_header (std::variant<std::string, http_header_t> key, const std::string& value) {
    if ( std::holds_alternative<http_header_t>(key) ) {
      return add_raw_header(to_string<std::string>(std::get<http_header_t>(key)), value);
    }
    if ( std::holds_alternative<std::string>(key) ) {
      return add_raw_header(std::get<std::string>(key), value);
    }
    return *this;
  }

  rest_api_response_i& add_header (std::variant<std::string, http_header_t> key, std::integral auto value) {
    return add_header(key, std::to_string(value));
  }

  rest_api_response_i& add_header (std::variant<std::string, http_header_t> key, const wbr::ConvertibleToString auto value) {
    return add_header(key, to_string<std::string>(value));
  }

  virtual void send(int http_code, content_type_t content_type)                               = 0;
  virtual void send_error(int http_code, content_type_t content_type, const std::string& msg) = 0;

protected:
  virtual rest_api_response_i& add_raw_header(const std::string& key, const std::string& value) = 0;
};

struct http_api_i {
  virtual ~http_api_i( ) = default;

  [[nodiscard]] virtual std::filesystem::path rsa_priv_key_path( ) const = 0;
  [[nodiscard]] virtual std::filesystem::path rsa_pub_key_path( ) const  = 0;
  [[nodiscard]] virtual std::filesystem::path ssl_cert_path( ) const     = 0;
  [[nodiscard]] virtual std::filesystem::path ssl_dh_path( ) const       = 0;

  virtual void set_rsa_priv_key_path(std::filesystem::path path) = 0;
  virtual void set_rsa_pub_key_path(std::filesystem::path path)  = 0;
  virtual void set_ssl_cert_path(std::filesystem::path path)     = 0;
  virtual void set_ssl_dh_path(std::filesystem::path path)       = 0;

  [[nodiscard]] virtual const std::string& rsa_private_key( ) const = 0;
  [[nodiscard]] virtual const std::string& rsa_public_key( ) const  = 0;

  virtual result_t<client_id_t> authorize_client(restbed::Session& session) const = 0;
};

struct plugin_api_i {
  enum class param_type_t { string, integer, boolean };

  struct param_spec_t {
    std::string_view name;
    param_type_t     type          = param_type_t::string;
    bool             required      = false;
    parameter_t      default_value = std::string { };
  };

  // A handler's result on success is the set of properties to send back to the
  // client (folded into the response body by the host); on failure it's the
  // HTTP status code plus a message - mirroring rest_api_response_i's
  // send()/send_error() split without the handler ever touching a response
  // object, restbed::Session, or content-type/format at all.
  struct handler_error_t {
    int         http_code;
    std::string message;
  };

  using handler_result_t = std::expected<parameter_map_t, handler_error_t>;

  // Two distinct handler shapes instead of one signature plus an
  // "auth required" bool a plugin author could set wrong: a handler that
  // takes a board_i& can only be registered through the board_handler_t
  // overload of add_resource, and the host authenticates the caller and
  // resolves *their* board before ever calling it - there is no path that
  // hands a board_i& to a handler without going through authentication
  // first. A resource that needs neither (or resolves its own board some
  // other way, e.g. by an explicit id) uses simple_handler_t instead. The
  // handler's own type is the declaration of what it needs, not a
  // separately-settable (and separately-forgettable) flag.
  using simple_handler_t = handler_result_t (*)(const parameter_map_t& params);
  using board_handler_t  = handler_result_t (*)(board_i& board, const parameter_map_t& params);

  struct resource_t {
    const std::string                                     path;
    const http_methods_t                                  method;
    const std::variant<simple_handler_t, board_handler_t> handler;
    const std::vector<param_spec_t>                       params { };
  };

  enum log_level_t { trace, debug, info, warn, error, critical };

  virtual ~plugin_api_i( ) = default;

  virtual void log(log_level_t level, std::string_view msg) const = 0;

  template<typename... Args>
  void log (log_level_t level, const fmt::format_string<Args...> fmt, Args&&... args) const {
    log(level, fmt::format(fmt, std::forward<Args>(args)...));
  }

  virtual void add_resource(std::string_view path, http_methods_t method, simple_handler_t handler, std::vector<param_spec_t> params = { }) = 0;
  virtual void add_resource(std::string_view path, http_methods_t method, board_handler_t handler, std::vector<param_spec_t> params = { })  = 0;

  void add_resource (const resource_t& resource) {
    std::visit([&] (auto handler) { add_resource(resource.path, resource.method, handler, resource.params); }, resource.handler);
  }

  [[nodiscard]] virtual size_t boards_count( ) const noexcept            = 0;
  virtual board_id_t           add_board(std::unique_ptr<board_i> board) = 0;
  // Plain C-style callback + opaque user_data, deliberately not std::function
  // or a capturing lambda: those cross the plugin/app ABI boundary as
  // type-erased objects whose manager/invoker code is compiled wherever the
  // callable is instantiated (i.e. inside the plugin's .so). If such an
  // object outlives dlclose()-ing that plugin, destroying or invoking it
  // jumps into unmapped memory. user_data carries per-call context instead.
  using board_manipulation_func_t                                                  = void (*)(void* user_data, board_id_t, board_i&);
  virtual void     for_each_board(board_manipulation_func_t func, void* user_data) = 0;
  virtual board_i& board(board_id_t board_id)                                      = 0;

  virtual std::optional<client_id_t> add_new_client(board_id_t board_id)     = 0;
  virtual std::optional<board_i&>    board_for_client(client_id_t client_id) = 0;

  virtual std::unique_ptr<board_i> create_board(std::size_t width, std::size_t height, int bombs_count) = 0;

  virtual void install_entrypoints(restbed::Service& service) = 0;

  virtual http_api_i* http_api( ) = 0;

#if USE_PALSIGSLOT
  virtual sigslot::signal<>& ready_to_load_resources_signal( ) = 0;
  virtual void               on_ready_to_load_resources( )     = 0;
#endif
};

using param_type_t     = plugin_api_i::param_type_t;
using param_spec_t     = plugin_api_i::param_spec_t;
using handler_error_t  = plugin_api_i::handler_error_t;
using handler_result_t = plugin_api_i::handler_result_t;
