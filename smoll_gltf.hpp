#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstddef>
#include <cstring>
#include <vector>
#include <string_view>

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
  char8_t const * data;
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
    String uri;
    uint64_t byte_length;
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

enum class Symbol{
  none= 0,
  open_square,
  close_square,
  open_squigily,
  close_squigily,
  begin_string,
  end_string,
  colon,
  comma,
};

auto count_json_symbols = [](char const * const json, uint32_t json_length) constexpr noexcept -> uint32_t{
  uint32_t token_count = 0;
  for(auto char_offset = 0; char_offset < json_length; ++char_offset){
    switch(json[char_offset]){
      case '"': case '{' : case '}' : case '[' : case ']' : case ':' : case ',' : ++ token_count;
    }
  }
  return token_count;
};

auto parse_json_symbols = [](char const * const json, uint32_t json_length, uint32_t * offsets, Symbol * symbols) constexpr noexcept -> Error{
  uint32_t current_token_offset = 0;
  Symbol current_token = Symbol::none;
  bool begin_string = false;

  auto test_character = [&](char character, uint32_t offset) constexpr noexcept{
    auto set_current_token = [&](Symbol symbol) constexpr noexcept{
      symbols[current_token_offset] = symbol;
      offsets[current_token_offset] = offset;
      ++current_token_offset;
    };

    if(begin_string){
      if(character == '"'){
        begin_string = false;
        return set_current_token(Symbol::end_string);
      }
    }

    switch(character){
      case '"' : if(not begin_string){
        begin_string = true;
        return set_current_token(Symbol::begin_string);   
      } 
      case '{' : return set_current_token(Symbol::open_squigily);
      case '}' : return set_current_token(Symbol::close_squigily);
      case '[' : return set_current_token(Symbol::open_square);
      case ']' : return set_current_token(Symbol::close_square);
      case ':' : return set_current_token(Symbol::colon);
      case ',' : return set_current_token(Symbol::comma);
    }
  };

  for(auto char_offset = 0; char_offset < json_length; ++char_offset){
    test_character(json[char_offset], char_offset);
  }
  return Error::no_problem;
};



//WARNGING this can overflow if the javascript symbols add up to be more than the size of the stack.
constexpr Error parse_gltf(byte const * const raw, uint64_t raw_length, GLTF &gltf, auto allocator) noexcept{
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

  enum Parse_Item{
    none,
    root,
    tag,
    define,
    array,
    value,
  };

  enum class Root_Item{
    none,
    asset,
  };

  uint8_t stack_depth = -1;
  Parse_Item stack_items[UINT8_MAX];
  String stack_tags[UINT8_MAX];
  uint32_t stack_offsets[UINT8_MAX];
  Root_Item root_item = none;
  uint8_t current_stack_definition_tag;

  uint32_t begin_string_offset = -1;

  for(auto symbol_index = 0; symbol_index < symbol_count; ++symbol_index){
    auto symbol = symbols[symbol_index];
    auto offset = symbol_offsets[symbol_index];

    Parse_Item item;
    if(stack_depth < 0){
      item = none;
    } else {
      item = stack_items[stack_depth];
    }

    switch(symbol){

      case Symbol::begin_string:{
        begin_string_offset = offset;
      }

      case Symbol::end_string:{
        if(begin_string_offset < 0) return Error::problem;
        auto string = String{
          .data = reinterpret_cast<char8_t const *>(json_chunk.data + offset),
          .length = offset - begin_string_offset,
        };
        if(item == root){
          if(memcmp(string.data, "asset", string.length)){
            root_item = Root_Item::asset;
          }
        }
        ++stack_depth;
        stack_items[stack_depth] = tag;
        stack_tags[stack_depth] = string;
        continue;
      }

      case Symbol::colon:{
        current_stack_definition_tag = stack_depth;
        ++stack_depth;
        stack_items[stack_depth] = define;
        stack_offsets[stack_depth] = offset;
        continue;
      }

      case Symbol::open_squigily:{
        if(item == none){
          stack_depth++;
          stack_items[0] = root;
          continue;
        }
        //All tags should increase the depth when they are parsed so this implise an empty {} in the root object.
        if(item == root){
          return Error::problem;
        }
      }
      case Symbol::close_squigily:{
        //End of json
        if(item == root){
          //Should then be -1.
          --stack_depth;
        }
      }
      case Symbol::open_square:{

      }
      case Symbol::close_square:{

      }

      case Symbol::comma:{
        //if(stack_items[stack_depth] == tag){
        //  auto definition = stack_tags[stack_depth - 2];
        //  if(root_item == Root_Item::asset){
        //    if(memcmp(definition.data,"generator",definition.length)){
        //      gltf.asset.generator = stack_tags[stack_depth];
        //    }
        //    if(memcmp(definition.data, "version", definition.length)){
        //      gltf.asset.version = stack_tags[stack_depth];
        //    }
        //  }

        //}else if(stack_items[stack_depth] == define){
          //Must be a number value,

        }
      }

      case Symbol::none: return Error::problem;
    }
  }
  

  return Error::no_problem;
}

}
