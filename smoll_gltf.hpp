#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstddef>
#include <cstring>

namespace smoll{

constexpr uint32_t glTF = 0x46546C67;
constexpr uint32_t JSON = 0x4E4F534A;
constexpr uint32_t BIN = 0x004E4942;

using byte = uint8_t;

enum class Error: int32_t{
  no_problem=0,
  problem = 1,
  not_glTF = -1
};

struct String{
  char8_t * data;
  uint32_t length;
};

struct GLTF{
  struct Asset{
    //in data
    String generator;
    //in data
    //Maybe just a number or enum.
    String version;
  } asset;
  uint32_t scene;
  struct Scene{

    String name;

    struct Node{
      String name;
      float translation[3];
      float rotation[4];
      float scale[3];
      float matrix[4][4];
      uint32_t * children;
    } * nodes;

  } * scenes;

  struct Mesh{
    String name;
    struct Primitive{
      struct Attribute{
        uint16_t
          JOINTS_0,
          TEXCOORD_0,
          WEIGHTS_0,
          NORMAL,
          POSITION;
      } * attributes;
      uint16_t indices;
      uint16_t material;
      uint16_t mode;
    } * primitives;
  } * meshes;

  struct Accessor{

  };

  struct Buffer_View{

  } * buffer_views;

  struct Buffer{

  };

  uint32_t node_count;
  uint32_t mesh_count;
  uint32_t accessor_count;
  uint32_t buffer_view_count;
  uint32_t buffer_count;
};

struct Header{
  uint32_t magic;
  uint32_t version;
  uint32_t length;
};
  
struct Chunk{
  uint32_t length;
  uint32_t type;
  byte const * data;
};

struct Asset{

};

enum class Symbol{
  none= 0,
  open_square,
  close_square,
  open_squigily,
  close_squigily,
  begin_string,
  end_string,
  colon,
};

constexpr uint32_t count_json_symbols(char const * const json, uint32_t json_length){
  uint32_t token_count = 0;
  for(auto char_offset = 0; char_offset < json_length; ++char_offset){
    switch(json[char_offset]){
      case '"': case '{' : case '}' : case '[' : case ']' : case ':' : ++ token_count;
    }
  }
  return token_count;
}

constexpr Error parse_json_symbols(char const * const json, uint32_t json_length, uint32_t * offsets, Symbol * symbols){
  uint32_t current_token_offset = 0;
  Symbol current_token = Symbol::none;
  bool begin_string = false;

  auto set_current_token = [&](Symbol symbol, uint32_t offset) constexpr noexcept{
    symbols[current_token_offset] = symbol;
    offsets[current_token_offset] = offset;
    ++current_token_offset;
  };

  auto test_character = [&](char character, uint32_t offset) constexpr noexcept{
    if(begin_string){
      if(character == '"'){
        begin_string = false;
        return set_current_token(Symbol::end_string, offset);
      }
    }

    switch(character){
      case '"' : if(not begin_string) return set_current_token(Symbol::begin_string, offset);   
      case '{' :return set_current_token(Symbol::open_squigily, offset);
      case '}' :return set_current_token(Symbol::close_squigily, offset);
      case '[' :return set_current_token(Symbol::open_square, offset);
      case ']' :return set_current_token(Symbol::close_square, offset);
      case ':' :return set_current_token(Symbol::colon, offset);
    }
  };

  for(auto char_offset = 0; char_offset < json_length; ++char_offset){
    test_character(json[char_offset], char_offset);
  }
  return Error::no_problem;
}

//WARNGING this can overflow if the javascript symbols add up to be more than the size of the stack.
constexpr Error parse_gltf(byte const * const raw, uint64_t raw_length, GLTF * gltf, auto allocator) noexcept{
  if(raw_length < 20){
    return Error::problem;
  }

  Header header;
  memcpy(&header.magic, raw, 4);
  if(header.magic not_eq glTF){
    return Error::not_glTF;
  }

  memcpy(&header.version, raw + 4, 4);
  memcpy(&header.length, raw + 8, 4);

  Chunk json_chunk;
  memcpy(&json_chunk.length, raw + 12, 4);
  memcpy(&json_chunk.type, raw + 16, 4);
  if(json_chunk.type not_eq JSON){
    return Error::problem;
  }
  json_chunk.data = raw + 20;

  Chunk binary_chunk;
  memcpy(&binary_chunk.length, raw + 20 + json_chunk.length, 4);
  memcpy(&binary_chunk.type, raw + 20 + json_chunk.length + 4, 4);
  if(json_chunk.type != BIN){
    return Error::problem;
  }
  binary_chunk.data = raw + 20 + json_chunk.length + 8; 

  auto json = reinterpret_cast<char const *>(json_chunk.data);
  auto symbol_count = count_json_symbols(json, json_chunk.length);
  Symbol symbols[symbol_count];
  uint32_t symbol_offsets[symbol_count];
  parse_json_symbols(json, json_chunk.length, symbol_offsets, symbols);

  return Error::no_problem;
}

}
