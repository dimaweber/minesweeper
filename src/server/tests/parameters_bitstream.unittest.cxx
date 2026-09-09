#include <gtest/gtest.h>

#include <iostream>

#include "../plugins/api.hxx"

[[maybe_unused]] parameter_t             p;    // make sure parameter_t is defined and accessible
[[maybe_unused]] parameter_bytestream_t* pbs;  // make sure parameter_bytestream_t is defined and accessible

std::ostream& operator<< (std::ostream& os, const parameter_t& p) {
  std::visit([&os] (const auto& v) {
    using T = std::decay_t<decltype(v)>;
    if constexpr ( std::is_same_v<T, parameter_list_t> ) {
      os << "[";
      for ( const auto& item: v ) {
        os << item << ", ";
      }
      os << "]";
    } else if constexpr ( std::is_same_v<T, parameter_map_t> ) {
      os << "{";
      for ( const auto& [key, item]: v ) {
        os << key << ": " << item << ", ";
      }
      os << "}";
    } else {
      os << v;
    }
  }, p);
  return os;
}

TEST (ParametersBitstream, Integers) {
  for ( const auto i: std::initializer_list<int64_t> {
            -0x08'00'00'12'0f'00'00'00,
            -100,
            -1,
            0,
            1,
            42,
            100,
            1000,
            10000,
            0x7fffffff,
            0x80000000,
            0xffffffff,
            0x7fbcd98ef012378LL,
            0xfffffffeaffffffLL,
        } ) {
    const parameter_t          p_int = i;
    std::array<std::byte, 100> buffer;
    buffer.fill(std::byte {0xfa});

    parameter_bytestream_t pbs_write(buffer);
    pbs_write.serialize(p_int);

    parameter_bytestream_t pbs_read(buffer);
    const parameter_t      out = pbs_read.deserialize( );

    EXPECT_EQ(out, p_int);
  }
}

TEST (ParametersBitstream, Strings) {
  const parameter_t          p_str = std::string("Hello, World!");
  std::array<std::byte, 100> buffer;
  buffer.fill(std::byte {0xfa});

  parameter_bytestream_t pbs_write(buffer);
  pbs_write.serialize(p_str);

  parameter_bytestream_t pbs_read(buffer);
  const parameter_t      out = pbs_read.deserialize( );

  EXPECT_EQ(out, p_str);
}

TEST (ParametersBitstream, Booleans) {
  const parameter_t          p_bool = true;
  std::array<std::byte, 100> buffer;
  buffer.fill(std::byte {0xfa});

  parameter_bytestream_t pbs_write(buffer);
  pbs_write.serialize(p_bool);

  parameter_bytestream_t pbs_read(buffer);
  const parameter_t      out = pbs_read.deserialize( );

  EXPECT_EQ(out, p_bool);
}

TEST (ParametersBitstream, IntegersList) {
  const parameter_t          p_list = parameter_list_t {1, 2, 3};
  std::array<std::byte, 100> buffer;
  buffer.fill(std::byte {0xfa});

  parameter_bytestream_t pbs_write(buffer);
  pbs_write.serialize(p_list);

  parameter_bytestream_t pbs_read(buffer);
  const parameter_t      out = pbs_read.deserialize( );

  EXPECT_EQ(out, p_list);
}

TEST (ParametersBitstream, StringsList) {
  const parameter_t          p_list = parameter_list_t {"one", "two", "three"};
  std::array<std::byte, 100> buffer;
  buffer.fill(std::byte {0xfa});

  parameter_bytestream_t pbs_write(buffer);
  pbs_write.serialize(p_list);

  parameter_bytestream_t pbs_read(buffer);
  const parameter_t      out = pbs_read.deserialize( );

  EXPECT_EQ(out, p_list);
}

TEST (ParametersBitstream, IntegersListsList) {
  const parameter_t p_list = parameter_list_t {
      parameter_list_t {1,   2  },
       parameter_list_t {30,  40 },
       parameter_list_t {500, 600}
  };
  std::array<std::byte, 100> buffer;
  buffer.fill(std::byte {0xfa});

  parameter_bytestream_t pbs_write(buffer);
  pbs_write.serialize(p_list);

  parameter_bytestream_t pbs_read(buffer);
  const parameter_t      out = pbs_read.deserialize( );

  EXPECT_EQ(out, p_list);
}

TEST (ParametersBitstream, Map) {
  const parameter_t p_map = parameter_map_t {
      {"0x0",   0                               },
      {"0x1",   1                               },
      {"0x2",   2                               },
      {"0x3",   3                               },
      {"0x4",   4                               },
      {"0x5",   5                               },
      {"0x6",   6                               },
      {"hex",   "hex values"                    },
      {"dec",   "decimal values"                },
      {"bin",   "binary values"                 },
      {"ok",    true                            },
      {"array", parameter_list_t {1, 2, 3, 4, 5}},
      {"map",
       parameter_map_t {
              {"nested_key1", "nested_value1"},
              {"nested_key2", "nested_value2"},
          }                                     },
  };
  std::array<std::byte, 0x1000> buffer;
  buffer.fill(std::byte {0xfa});

  parameter_bytestream_t pbs_write(buffer);
  pbs_write.serialize(p_map);

  parameter_bytestream_t pbs_read(buffer);
  const parameter_t      out = pbs_read.deserialize( );

  EXPECT_EQ(out, p_map);
}
