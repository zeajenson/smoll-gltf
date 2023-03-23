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

auto count_json_symbols = [](char8_t const * const json, uint32_t json_length) constexpr noexcept -> uint32_t{
  uint32_t token_count = 0;
  for(auto char_offset = 0; char_offset < json_length; ++char_offset){
    switch(json[char_offset]){
      case '"': case '{' : case '}' : case '[' : case ']' : case ':' : case ',' : ++ token_count;
    }
  }
  return token_count;
};

auto parse_json_symbols = [](char8_t const * const json, uint32_t json_length, uint32_t * offsets, Symbol * symbols) constexpr noexcept -> Error{
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
    }else switch(character){
      case '"' :
        begin_string = true;
        return set_current_token(Symbol::begin_string);   
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

  auto json = reinterpret_cast<char8_t const *>(json_chunk.data);
  auto symbol_count = count_json_symbols(json, json_chunk.length);
  Symbol symbols[symbol_count];
  uint32_t symbol_offsets[symbol_count];
  parse_json_symbols(json, json_chunk.length, symbol_offsets, symbols);

  enum Parse_Item{
    none,
    root,
    tag,
    define,
    definition,
    value,
    array,
    object
  };

  enum class glTF_Item{
    asset,
    scenes,
    nodes,
    meshes__primitives,
    meshes__primitives__attributes
  };

  int16_t stack_depth = -1;
  Parse_Item stack_items[UINT8_MAX];
  String stack_tags[UINT8_MAX];
  uint32_t stack_offsets[UINT8_MAX];
  //Represents the index of the current item being parsed in whatever array on the stack
  uint32_t stack_array_current_indices[UINT8_MAX] = {0};
  uint32_t stack_array_sizes[UINT8_MAX] = {0};
  int32_t current_array_stack_index = -1;

  auto update_current_array_stack_index = [&] constexpr noexcept{
    for(auto stack_index = stack_depth; stack_index > 0; --stack_index){
      if(stack_items[stack_index] == array){
        current_array_stack_index = stack_index;
        return;
      }
    }
    current_array_stack_index = -1;
  };

  bool in_array = false;

  glTF_Item current_gltf_item = none;
  uint8_t current_stack_definition_tag;

  int32_t begin_string_offset = -1;

  auto add_array_to_stack_and_init_array_in_gltf_object = [&]() constexpr noexcept{
    //TODO: calculate array size.
    uint32_t array_size =0;
    ++stack_depth;
    stack_items[stack_depth] = array;
    stack_array_sizes[stack_depth] = array_size;
    stack_array_current_indices[stack_depth] = 0;

    switch(current_gltf_item){
      case glTF_Item::scenes: {
        gltf.scenes = allocator(sizeof(GLTF::Scene) * array_size);
      };
    }
  };

  auto set_value_in_gltf_object_and_decrease_stack_depth = [&](String value) constexpr noexcept{

  };

  auto set_value_in_gltf_array = [&]() constexpr noexcept{

  };

  auto set_value_in_glTF_value_array = [&](uint32_t symbol_index){

  };
  //NEWNEWNENWNEWNENE
  //NEWNEWNENWNEWNENE

  auto add_defintion_to_stack = [&](String definition){
    ++stack_depth;
    stack_items[stack_depth] = definition;
    stack_tags[stack_depth] = definition;
  };

  auto add_object_to_stack = [&]{
    ++stack_depth;
    stack_items[stack_depth] = object;
  };

  auto add_gltf_root_item_to_stack = [&](String string){
    if(memcmp(string.data, "asset", string.length)){
      add_defintion_to_stack(string);
      current_gltf_item = glTF_Item::asset;
    }
  };

  if(symbols[0] not_eq Symbol::open_squigily){
    return Error::problem;
  }else{
    stack_depth = 0;
    stack_items[stack_depth] = root;
  }

  for(auto symbol_index = 1; symbol_index < symbol_count; ++symbol_index){
    auto current_symbol = symbols[symbol_index];
    auto current_stack_item = stack_items[stack_depth];
    bool has_string = false;
    String string;

    if(current_symbol == Symbol::begin_string){
      begin_string_offset = symbol_offsets[symbol_index] + 1;
      continue;
    }else if(current_symbol == Symbol::end_string){
      has_string = true;
      auto string = String{
        .data = json + begin_string_offset,
        .length = (symbol_offsets[symbol_index] - 1) - begin_string_offset
      };
    }

    switch(current_stack_item){
      case root : 
        if(has_string) 
          add_gltf_root_item_to_stack(string); 
        //Should be imposible to get hear unless the file is malformed.
        else return Error::problem;
        continue;
      case definition : 
        if(has_string) set_value_in_gltf_object_and_decrease_stack_depth(string);
        else switch(current_symbol){
          case Symbol::open_squigily: 
            add_object_to_stack(); continue;
          case Symbol::open_square:
            add_array_to_stack_and_init_array_in_gltf_object(); continue;
          //Must be a value
          case Symbol::comma:
          default:continue;
        } continue;
      case object:
        if(has_string) add_defintion_to_stack(string); continue;
      case array: 
        if(has_string){

          continue;
        }else switch(current_symbol){
          case Symbol::open_squigily:
            add_object_to_stack(); continue;
          //There are no arrays of arrays in the gltf spec.
          case Symbol::open_square: return Error::problem;

          case Symbol::comma: 
          default: continue;
        }
        continue;
    }
  }

  //NEWNEWNENWNEWNENE
  //NEWNEWNENWNEWNENE
      //case Symbol::open_square:{
      //  auto array_size = 0;
      //  uint32_t unmatched_open_squigily = 0;
      //  uint32_t extra_open_square = 0;
      //  
      //  for(auto array_symbol_index = symbol_index; array_symbol_index < symbol_count; ++ array_symbol_index){
      //    switch(symbols[array_symbol_index]){
      //      case Symbol::comma:{
      //        if(unmatched_open_squigily == 0 && extra_open_square == 0){
      //          ++array_size;
      //        }
      //        continue;
      //      }

      //      case Symbol::open_squigily: ++unmatched_open_squigily; continue;
      //      case Symbol::close_squigily:{
      //        --unmatched_open_squigily;
      //        continue;
      //      }

      //      case Symbol::open_square: ++extra_open_square; continue;
      //      case Symbol::close_square: {
      //        --extra_open_square;
      //        if(extra_open_square == -1){
      //          if(array_size > 0) ++array_size;
      //          goto at_end_of_array;
      //        }
      //        continue;
      //      }
      //      default:continue;
      //    }
      //  }
      //  at_end_of_array:
      //  add_array_to_stack_and_init_array_in_gltf_object(array_size);
      //  update_current_array_stack_index();
      //  continue;
      //}
  return Error::no_problem;
}

}
