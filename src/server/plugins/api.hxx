#pragma once

#include <fmt/format.h>

#include <corvusoft/restbed/status_code.hpp>
#include <expected>
#include <filesystem>
#include <functional>
#include <map>
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

/// Bump this whenever addon_api_i/plugin_api_i/board_i/cell_i change shape in any way
/// that would make an already-built plugin call the wrong vtable slot (reordering,
/// removing, or changing the signature of an existing virtual method) - new methods
/// appended at the end don't need a bump. See addon_api_abi_tag() below: it folds
/// this in alongside compiler/stdlib identity so a mismatched plugin is refused at
/// load time instead of corrupting memory once called.
#define ADDON_API_ABI_VERSION 2

/// Everything crossing the addon_api_i/plugin_api_i/board_i boundary - including this
/// header itself, since sigslot::signal<> is header-only and its layout is whatever
/// each translation unit's compiler/flags produce - only has a well-defined, agreed
/// layout when the plugin and the server are built with the same compiler, same
/// standard library, and the same version of this header. There is no portable way
/// to make a C++ virtual-interface ABI like this one safe across arbitrary
/// compilers/standard libraries (that needs a plain-C ABI instead); this tag exists
/// only to turn a silent mismatch into a loud, logged refusal to load, rather than a
/// memory-corruption crash somewhere unrelated later on.
/// static, not inline: this must never become an exported, externally-linked
/// symbol. An inline (weak, default-visibility) definition here would be
/// resolved via the process's global symbol scope - and an executable's own
/// exported symbols always win that resolution for anything it dlopen()s,
/// regardless of RTLD_LOCAL on the loaded library. That silently interposes
/// the *host's* copy of this function into every plugin's call to it,
/// making the whole ABI check compare the host's tag against itself no
/// matter what the plugin was actually built with. static (internal
/// linkage) gives every translation unit - the host and each plugin - its
/// own private, non-exported copy that can't be interposed.
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

/// Every plugin must invoke this exactly once, at namespace scope, to export the
/// abi_tag() symbol the server checks (via dlsym) before calling init_plugin(). A
/// plugin that doesn't export it is refused just like one missing init_plugin.
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

  /// Auto-reveal: opening a cell with 0 neighbouring mines recursively opens all
  /// of its neighbours (and, transitively, their neighbours), but this flood-fill
  /// can never open a mine.
  [[nodiscard]] virtual std::vector<reveal_result_t> reveal_cells(const coord_t& coord) = 0;

protected:
  [[nodiscard]] virtual bool is_valid_x(int x) const noexcept = 0;
  [[nodiscard]] virtual bool is_valid_y(int y) const noexcept = 0;
};

using board_id_t = uint64_t;

using client_id_t = uint64_t;

using headers_t = std::multimap<std::string, std::string>;

/// A parameter is either a scalar value, an array of parameters of the same
/// (recursive) type (arrays of arrays are allowed), or a map of named
/// parameters of the same (recursive) type (maps of maps/arrays, and vice
/// versa, are allowed too). This is implemented as a variant deriving struct
/// so that `std::vector<parameter_t>` / `std::unordered_map<std::string,
/// parameter_t>` can appear as alternatives of `parameter_t` itself (allowed
/// since C++17 relaxed the incomplete-type requirements for `std::vector`;
/// libstdc++'s node-based `std::unordered_map` supports incomplete mapped
/// types the same way in practice).
struct parameter_t : std::variant<std::string, int64_t, uint64_t, bool, std::vector<parameter_t>, std::unordered_map<std::string, parameter_t>> {
  using variant::variant;
};

using parameter_list_t = std::vector<parameter_t>;
using parameter_map_t  = std::unordered_map<std::string, parameter_t>;

template<typename T>
using result_t = std::expected<T, std::string>;

struct parameter_bytestream_t {
  /// @todo: zip/unzip and a stronger (e.g. sha256) signature don't belong
  ///        in this class - this header is ABI surface (built into every
  ///        plugin .so as well as the host), so it can only ever depend on
  ///        what's already a hard requirement for everyone building
  ///        against it, and pulling in a compression/crypto library for
  ///        that would be a permanent dependency tax on every plugin,
  ///        forever, for a same-process boundary that doesn't actually
  ///        have an adversary or a bandwidth problem. If/when this type
  ///        moves to its own library outside the plugin ABI (see below),
  ///        those belong there instead, as wrappers around the raw
  ///        store()/load() bytes rather than something this class knows
  ///        about - compress/sign before store()'s bytes go wherever
  ///        they're going, decompress/verify before they reach load().
  /// @todo: this type is genuinely useful beyond the plugin ABI - worth
  ///        splitting into its own small library some day, at which point
  ///        the header-only constraint (forced by every plugin .so and
  ///        the host needing an identical, ABI-tag-checked copy of this
  ///        exact header) goes away too, opening up things that need a
  ///        .cxx (e.g. a table-based CRC, or true concurrent partial-read
  ///        support with an unwind/rewind stack - speculative for now
  ///        since nothing here has a concurrent producer/consumer on one
  ///        buffer yet).
  parameter_bytestream_t (std::byte* buffer, size_t buffer_size) : buffer_(buffer), buffer_size_(buffer_size) {
  }

  explicit parameter_bytestream_t (std::span<std::byte> buffer) : buffer_(buffer.data( )), buffer_size_(buffer.size( )) {
  }

  /// Bump when serialize()/deserialize()'s wire grammar - or store()/load()'s
  /// own header shape below - changes in a way that would make an old reader
  /// misparse a new writer's bytes (new type_tag values appended at the end
  /// are fine; anything else isn't). Written/checked once per buffer by
  /// store()/load(), outside the recursive grammar itself -
  /// serialize()/deserialize() stay unaware it exists. Bumped to 2 for the
  /// length+checksum header fields below.
  static constexpr uint8_t wire_version = 2;

  /// Bytes actually written/consumed so far - lets a caller that store()d
  /// into a scratch buffer larger than it needed find out how much of it
  /// is real payload.
  [[nodiscard]] size_t size ( ) const noexcept {
    return static_cast<size_t>(offset_);
  }

  /// @{
  /// Public entry points: store()/load() are the only way in or out of a
  /// buffer, and neither ever lets an exception reach the caller - every
  /// internal failure (buffer overrun, malformed tag, version mismatch,
  /// checksum mismatch, reuse of an already-used instance) comes back as
  /// the error side of result_t, so a caller on the other side of a dlopen
  /// boundary can't forget to handle it and doesn't need to know this type
  /// can throw at all internally. serialize()/deserialize() are the
  /// recursive core and are deliberately private - a caller only ever
  /// deals in whole, versioned buffers.
  ///
  /// The header written here - version, then a fixed-width payload length
  /// and checksum - is deliberately outside serialize()/deserialize()'s own
  /// recursive, compact (numberX-tagged) grammar: it's a single one-time
  /// field per buffer, not a value repeated per array/map element, so the
  /// few extra fixed-width bytes don't matter and a fixed width means
  /// load() can find it without first having to parse anything.
  [[nodiscard]] result_t<void> store (const parameter_t& param) {
    if ( offset_ != 0 ) {
      return std::unexpected(fmt::format("parameter_bytestream_t::store: instance already used (offset {} != 0); use a fresh instance per buffer", offset_));
    }
    try {
      put_byte(wire_version);

      // Length and checksum are only known once serialize() has actually
      // run, so reserve their slot now and backfill it once the payload
      // is written.
      check_capacity(sizeof(uint64_t) + sizeof(uint32_t));
      const size_t header_pos = offset_;
      offset_ += sizeof(uint64_t) + sizeof(uint32_t);

      const size_t payload_pos = offset_;
      serialize(param);
      const size_t payload_len = offset_ - payload_pos;

      as_ref<uint64_t>(buffer_ + header_pos)                      = static_cast<uint64_t>(payload_len);
      as_ref<uint32_t>(buffer_ + header_pos + sizeof(uint64_t)) = crc32(buffer_ + payload_pos, payload_len);
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

      check_capacity(sizeof(uint64_t) + sizeof(uint32_t));
      const uint64_t payload_len       = as_ref<uint64_t>(buffer_ + offset_);
      const uint32_t expected_checksum = as_ref<uint32_t>(buffer_ + offset_ + sizeof(uint64_t));
      offset_ += sizeof(uint64_t) + sizeof(uint32_t);

      // Bytes past the real payload are whatever was already in the
      // buffer before this store() wrote it (uninitialized, or leftover
      // from an earlier use) - buffer_size_ alone can't tell an
      // oversized buffer from a truncated one, only the length this same
      // store() wrote can. Narrowing buffer_size_ here makes every
      // check_capacity() call for the rest of this load() - including
      // the whole recursive deserialize() descent - fail if it would
      // read past the real payload, not just past the physical buffer.
      check_capacity(payload_len);
      buffer_size_ = offset_ + payload_len;

      if ( crc32(buffer_ + offset_, payload_len) != expected_checksum ) {
        return std::unexpected(fmt::format("parameter_bytestream_t::load: checksum mismatch ({} byte payload)", payload_len));
      }

      const size_t     payload_pos = offset_;
      const parameter_t result     = deserialize( );
      if ( static_cast<size_t>(offset_) != payload_pos + payload_len ) {
        return std::unexpected(fmt::format("parameter_bytestream_t::load: {} trailing byte(s) after parsing a well-formed payload", payload_pos + payload_len - static_cast<size_t>(offset_)));
      }
      return result;
    } catch ( const std::exception& e ) {
      return std::unexpected(fmt::format("parameter_bytestream_t::load failed: {}", e.what( )));
    }
  }
  /// @}

private:
  /// Internal-only: serialize()/deserialize()'s recursive descent unwinds
  /// through this on any failure. It never crosses store()/load() - both
  /// catch std::exception and convert to result_t's error side before
  /// returning.
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

  /// Every read/write primitive below funnels through here first, so a
  /// truncated, corrupted, or maliciously short buffer fails loudly right
  /// where it would otherwise read/write out of bounds, instead of silently
  /// touching memory past buffer_size_.
  void check_capacity (size_t needed) const {
    if ( static_cast<size_t>(offset_) + needed > buffer_size_ ) {
      throw error(fmt::format("parameter_bytestream_t: buffer overrun at offset {} (need {} more bytes, capacity {})", offset_, needed, buffer_size_));
    }
  }

  /// How many bytes numberN/stringN needs to hold v - used up front so
  /// check_capacity() reserves exactly that much instead of always the
  /// 8-byte worst case, which is what write() actually used to check even
  /// though the whole point of numberN/stringN is that most values need
  /// far less.
  [[nodiscard]] static constexpr int bytes_needed (uint64_t v) noexcept {
    int n = 1;
    for ( ; n < 8 && v >= (uint64_t {1} << (n * 8)); ++n ) {
    }
    return n;
  }

  /// Plain bitwise CRC-32 (IEEE 802.3 / zlib polynomial) over the payload -
  /// no table, since payloads here are small enough (KB, not GB) that the
  /// per-byte cost is a non-issue, and a table would mean static
  /// initialization-order reasoning across every plugin that includes this
  /// header for no real benefit. This defends only against accidental
  /// corruption/truncation/version skew inside one host process - not
  /// against a tampering adversary, which is a different (and here,
  /// inapplicable) threat model.
  [[nodiscard]] static uint32_t crc32 (const std::byte* data, size_t len) noexcept {
    uint32_t crc = 0xFFFF'FFFFu;
    for ( size_t i = 0; i < len; ++i ) {
      crc ^= static_cast<uint8_t>(data[i]);
      for ( int bit = 0; bit < 8; ++bit ) {
        const uint32_t mask = -(crc & 1u);
        crc = (crc >> 1) ^ (0xEDB8'8320u & mask);
      }
    }
    return ~crc;
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
    check_capacity(sizeof(type_tag) + bytes_needed(str_size) + str_size);
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
    const uint64_t v = val >= 0 ? static_cast<uint64_t>(val) : -static_cast<uint64_t>(val);
    check_capacity(sizeof(type_tag) + bytes_needed(v));
    if ( val >= 0 ) {
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

struct plugin_api_i {
  enum class param_type_t { string, integer, boolean };

  struct param_spec_t {
    /// `std::string`, not `std::string_view`: unlike the handler payload
    /// itself, a resource's param specs now round-trip through owned,
    /// decoded bytes (see resource_wire below) on their way into the host's
    /// long-lived resource registry - a view would dangle once that
    /// decoding's own temporaries are gone. This also drops what used to be
    /// an unstated assumption that plugin authors only ever wrote `.name =
    /// "some-literal"` (the one case a `string_view` here didn't dangle).
    std::string  name;
    param_type_t type          = param_type_t::string;
    bool         required      = false;
    parameter_t  default_value = std::string { };
  };

  /// A handler's result on success is the set of properties to send back to the
  /// client (folded into the response body by the host); on failure it's the
  /// HTTP status code plus a message - mirroring rest_api_response_i's
  /// send()/send_error() split without the handler ever touching a response
  /// object, restbed::Session, or content-type/format at all.
  struct handler_error_t {
    int         http_code;
    std::string message;
  };

  using handler_result_t = std::expected<parameter_map_t, handler_error_t>;

  /// @{
  /// Two distinct handler shapes instead of one signature plus an
  /// "auth required" bool a plugin author could set wrong: a handler that
  /// takes a board_i& can only be registered through the board_handler_t
  /// overload of add_resource, and the host authenticates the caller and
  /// resolves *their* board before ever calling it - there is no path that
  /// hands a board_i& to a handler without going through authentication
  /// first. A resource that needs neither (or resolves its own board some
  /// other way, e.g. by an explicit id) uses simple_handler_t instead. The
  /// handler's own type is the declaration of what it needs, not a
  /// separately-settable (and separately-forgettable) flag.
  ///
  /// Both shapes speak strictly in bytes: params_buf/params_len is a
  /// store()d parameter_t (a parameter_map_t at the top level) prepared by
  /// the host; the handler store()s its own handler_result_t - wrapped via
  /// handler_wire::to_wire() - into out_buf (out_cap bytes) and returns how
  /// many bytes it wrote, or 0 on any failure (a valid store() is always
  /// at least 1 byte, so 0 is an unambiguous sentinel). No parameter_t,
  /// parameter_map_t, or std::string crosses this function-pointer call as
  /// a C++ object - a plugin author still writes an ordinary
  /// parameter_map_t-in/handler_result_t-out function, and registers it
  /// through `simple_handler_adapter<Handler>`/`board_handler_adapter<Handler>`
  /// (below), which does the store()/load() at this boundary so no plugin
  /// has to.
  using simple_handler_t = size_t (*)(const std::byte* params_buf, size_t params_len, std::byte* out_buf, size_t out_cap);
  using board_handler_t  = size_t (*)(board_i& board, const std::byte* params_buf, size_t params_len, std::byte* out_buf, size_t out_cap);
  /// @}

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

  /// @{
  /// The actual ABI-crossing entry points: `spec_buf`/`spec_len` is a
  /// store()d resource_wire::spec_t - `path`, `method`, and the
  /// `param_spec_t` list packed into one parameter_t - the same byte-only
  /// treatment simple_handler_t/board_handler_t's own params/result
  /// already get (see the @note on those two aliases above). Only
  /// `handler` (a plain function pointer - safe regardless of which
  /// side's compiler/stdlib built it) crosses this vtable call as
  /// anything other than raw bytes. Plugin authors don't call these
  /// directly - use the add_resource(path, method, handler, params) or
  /// add_resource(const resource_t&) overloads below, which do the
  /// store() at this boundary instead.
  virtual void add_resource(const std::byte* spec_buf, size_t spec_len, simple_handler_t handler) = 0;
  virtual void add_resource(const std::byte* spec_buf, size_t spec_len, board_handler_t handler)  = 0;
  /// @}

  /// @{
  /// Ordinary registration - same call-site shape plugin authors have
  /// always used; only the serialization onto the byte-only overloads
  /// above is new. Defined out-of-line, after resource_wire, below.
  void add_resource (std::string_view path, http_methods_t method, simple_handler_t handler, std::vector<param_spec_t> params = { });
  void add_resource (std::string_view path, http_methods_t method, board_handler_t handler, std::vector<param_spec_t> params = { });
  /// @}

  void add_resource (const resource_t& resource) {
    std::visit([&] (auto handler) { add_resource(resource.path, resource.method, handler, resource.params); }, resource.handler);
  }

  [[nodiscard]] virtual size_t boards_count( ) const noexcept            = 0;
  virtual board_id_t           add_board(std::unique_ptr<board_i> board) = 0;
  /// Plain C-style callback + opaque user_data, deliberately not std::function
  /// or a capturing lambda: those cross the plugin/app ABI boundary as
  /// type-erased objects whose manager/invoker code is compiled wherever the
  /// callable is instantiated (i.e. inside the plugin's .so). If such an
  /// object outlives dlclose()-ing that plugin, destroying or invoking it
  /// jumps into unmapped memory. user_data carries per-call context instead.
  using board_manipulation_func_t                                                  = void (*)(void* user_data, board_id_t, board_i&);
  virtual void     for_each_board(board_manipulation_func_t func, void* user_data) = 0;
  virtual board_i& board(board_id_t board_id)                                      = 0;

  virtual std::optional<client_id_t> add_new_client(board_id_t board_id)     = 0;
  virtual std::optional<board_i&>    board_for_client(client_id_t client_id) = 0;

  virtual std::unique_ptr<board_i> create_board(std::size_t width, std::size_t height, int bombs_count) = 0;

#if USE_PALSIGSLOT
  virtual sigslot::signal<>& ready_to_load_resources_signal( ) = 0;
  virtual void               on_ready_to_load_resources( )     = 0;
#endif

  /// Same as create_board(), but with an explicit, caller-supplied mine
  /// layout instead of one placed by rand() - useful for a reproducible
  /// board (e.g. one matched against a real client session for testing),
  /// since rand()'s actual output isn't standardized across
  /// platforms/compilers/libc and gives no portability guarantee even with
  /// a fixed seed. Appended here (after every other method, including the
  /// `#if USE_PALSIGSLOT` block) rather than next to create_board() so it's
  /// unconditionally the last vtable slot regardless of that macro - an
  /// append at the true end never needs an ADDON_API_ABI_VERSION bump.
  virtual std::unique_ptr<board_i> create_fixed_board(std::size_t width, std::size_t height, std::vector<coord_t> mines) = 0;
};

using param_type_t     = plugin_api_i::param_type_t;
using param_spec_t     = plugin_api_i::param_spec_t;
using handler_error_t  = plugin_api_i::handler_error_t;
using handler_result_t = plugin_api_i::handler_result_t;

/// Converts a resource registration's path/method/param-specs to/from the
/// single parameter_t envelope that crosses plugin_api_i::add_resource's
/// byte-only ABI boundary - the same byte-only treatment handler_wire
/// (below) gives a handler's own params/result, so std::string_view/
/// std::string/std::vector/std::variant never cross that call as C++
/// objects either.
namespace resource_wire {

struct spec_t {
  std::string               path;
  http_methods_t            method;
  std::vector<param_spec_t> params;
};

inline parameter_t to_wire (std::string_view path, http_methods_t method, const std::vector<param_spec_t>& params) {
  parameter_list_t param_list;
  param_list.reserve(params.size( ));
  for ( const auto& p: params ) {
    param_list.push_back(parameter_map_t {
        {"name",          p.name                       },
        {"type",          static_cast<int64_t>(p.type)},
        {"required",      p.required                  },
        {"default_value", p.default_value              },
    });
  }
  return parameter_map_t {
      {"path",   std::string(path)     },
      {"method", static_cast<int64_t>(method)},
      {"params", std::move(param_list) },
  };
}

/// A malformed envelope (only possible from a version-skewed or otherwise
/// broken caller - see handler_wire::from_wire's identical reasoning
/// below) is reported as `std::nullopt`; plugin_api_t::add_resource is
/// what actually logs about it, since that's host-internal and this
/// header has no logger of its own to call.
inline std::optional<spec_t> from_wire (const parameter_t& wire) {
  try {
    const auto& m = std::get<parameter_map_t>(wire);
    spec_t      spec;
    spec.path   = std::get<std::string>(m.at("path"));
    spec.method = static_cast<http_methods_t>(std::get<int64_t>(m.at("method")));
    for ( const auto& item: std::get<parameter_list_t>(m.at("params")) ) {
      const auto& pm = std::get<parameter_map_t>(item);
      spec.params.push_back(param_spec_t {
          .name          = std::get<std::string>(pm.at("name")),
          .type          = static_cast<param_type_t>(std::get<int64_t>(pm.at("type"))),
          .required      = std::get<bool>(pm.at("required")),
          .default_value = pm.at("default_value"),
      });
    }
    return spec;
  } catch ( const std::exception& ) {
    return std::nullopt;
  }
}

/// Growing-store idiom identical in spirit to api.cxx's own (internal)
/// store_growing() - pure serialization, no side effects, so retrying
/// with more room on failure is safe. Duplicated here rather than shared,
/// since api.cxx isn't visible to plugin translation units and this
/// header has to stay self-contained.
inline constexpr size_t initial_wire_capacity = 4096;
inline constexpr size_t max_wire_capacity     = 16u * 1024 * 1024;

inline std::vector<std::byte> store_growing (const parameter_t& value) {
  for ( size_t capacity = initial_wire_capacity; capacity <= max_wire_capacity; capacity *= 2 ) {
    std::vector<std::byte> buffer(capacity);
    parameter_bytestream_t bs(buffer);
    if ( const auto stored = bs.store(value); stored ) {
      buffer.resize(bs.size( ));
      return buffer;
    }
  }
  return { };  // empty => caller treats this as "failed to serialize"
}

}  // namespace resource_wire

inline void plugin_api_i::add_resource (std::string_view path, http_methods_t method, simple_handler_t handler, std::vector<param_spec_t> params) {
  const auto bytes = resource_wire::store_growing(resource_wire::to_wire(path, method, params));
  add_resource(bytes.data( ), bytes.size( ), handler);
}

inline void plugin_api_i::add_resource (std::string_view path, http_methods_t method, board_handler_t handler, std::vector<param_spec_t> params) {
  const auto bytes = resource_wire::store_growing(resource_wire::to_wire(path, method, params));
  add_resource(bytes.data( ), bytes.size( ), handler);
}

/// Converts a handler_result_t to/from the single parameter_t envelope that
/// actually crosses add_resource's simple_handler_t/board_handler_t
/// boundary: {"ok": true, "body": `<parameter_map_t>`} on success,
/// {"ok": false, "http_code": `<int64>`, "message": `<string>`} on failure.
namespace handler_wire {
inline parameter_t to_wire (const handler_result_t& result) {
  if ( result ) {
    return parameter_map_t {
        {"ok",   true },
        {"body", *result}
    };
  }
  return parameter_map_t {
      {"ok",        false                                            },
      {"http_code", static_cast<int64_t>(result.error( ).http_code)},
      {"message",   result.error( ).message                        },
  };
}

/// A malformed envelope (missing key, wrong alternative - only possible
/// from a broken or version-mismatched handler) is reported the same way a
/// handler-reported failure is: there is no separate "the wire itself was
/// bad" channel, dispatch() only ever needs to know "serve this body" or
/// "send this error".
inline handler_result_t from_wire (const parameter_t& wire) {
  try {
    const auto& m = std::get<parameter_map_t>(wire);
    if ( std::get<bool>(m.at("ok")) ) {
      return std::get<parameter_map_t>(m.at("body"));
    }
    return std::unexpected(handler_error_t {
        static_cast<int>(std::get<int64_t>(m.at("http_code"))),
        std::get<std::string>(m.at("message")),
    });
  } catch ( const std::exception& e ) {
    return std::unexpected(handler_error_t {restbed::INTERNAL_SERVER_ERROR, fmt::format("Malformed handler response envelope: {}.", e.what( ))});
  }
}
}  // namespace handler_wire

/// @{
/// Registers an ordinary parameter_map_t-in/handler_result_t-out function
/// (the shape every handler in handlers.cxx/cell_check.cxx/boards_list.cxx
/// actually writes) as a plugin_api_i::simple_handler_t/board_handler_t -
/// the byte-only shape the ABI boundary requires. Handler is a non-type
/// template parameter (a plain function, possibly with internal linkage -
/// both are fine as of C++11), so each instantiation is itself an ordinary,
/// capture-free function - a valid simple_handler_t/board_handler_t value,
/// compiled by whichever side (host or plugin) registers it.
template<handler_result_t (*Handler)(const parameter_map_t& params)>
size_t simple_handler_adapter (const std::byte* params_buf, size_t params_len, std::byte* out_buf, size_t out_cap) {
  parameter_bytestream_t params_bs(const_cast<std::byte*>(params_buf), params_len);
  const auto             params = params_bs.load( );
  if ( !params || !std::holds_alternative<parameter_map_t>(*params) ) {
    return 0;
  }

  handler_result_t result;
  try {
    result = Handler(std::get<parameter_map_t>(*params));
  } catch ( const std::exception& e ) {
    result = std::unexpected(handler_error_t {restbed::INTERNAL_SERVER_ERROR, e.what( )});
  }

  parameter_bytestream_t out_bs(out_buf, out_cap);
  const auto             stored = out_bs.store(handler_wire::to_wire(result));
  return stored ? out_bs.size( ) : 0;
}

template<handler_result_t (*Handler)(board_i& board, const parameter_map_t& params)>
size_t board_handler_adapter (board_i& board, const std::byte* params_buf, size_t params_len, std::byte* out_buf, size_t out_cap) {
  parameter_bytestream_t params_bs(const_cast<std::byte*>(params_buf), params_len);
  const auto             params = params_bs.load( );
  if ( !params || !std::holds_alternative<parameter_map_t>(*params) ) {
    return 0;
  }

  handler_result_t result;
  try {
    result = Handler(board, std::get<parameter_map_t>(*params));
  } catch ( const std::exception& e ) {
    result = std::unexpected(handler_error_t {restbed::INTERNAL_SERVER_ERROR, e.what( )});
  }

  parameter_bytestream_t out_bs(out_buf, out_cap);
  const auto             stored = out_bs.store(handler_wire::to_wire(result));
  return stored ? out_bs.size( ) : 0;
}
/// @}
