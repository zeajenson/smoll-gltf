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
  char8_t const * data;
  uint32_t length;
};

auto compair = [](String string, const char * value) constexpr noexcept{
  if consteval{
    for(auto i = 0; i < string.length; ++i){
      if(value[i] != string.data[i]){
        return 0;
      }
    }
    return 1;
  }
  return memcmp(value, string.data, string.length);
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
    uint32_t * nodes;
  } * scenes;

  struct Node{
    String name;
    float translation[3];
    float rotation[4];
    float scale[3];
    float matrix[4][4];
    uint32_t * children;
  } * nodes;

  struct Mesh{
    String name;
    struct Primitive{
      struct Attribute{
        uint32_t
          JOINTS_0,
          TEXCOORD_0,
          WEIGHTS_0,
          NORMAL,
          POSITION;
      } attributes;
      uint32_t indices;
      uint32_t material;
      uint32_t mode;
    } * primitives;
  } * meshes;

  struct Accessor{
    uint32_t buffer_view;
    uint32_t byte_offset;
    uint32_t component_type;
    uint32_t count;
    //TODO: support all sorts of types
    union Max_Min
    {
      float mat4[16];
    } max, min;
    String type;

  } * accessors;

  struct Buffer_View{
    uint32_t buffers;
    uint32_t byte_length;
    uint32_t byte_offset;
    uint32_t target;
  } * buffer_views;

  struct Buffer{
    String uri;
    uint64_t byte_length;
  } * buffers;

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
    meshes,
    accessors,
    buffer_views,
    buffers,
  };

  int16_t stack_depth = -1;
  Parse_Item stack_items[UINT8_MAX];
  String stack_tags[UINT8_MAX];
  uint32_t stack_offsets[UINT8_MAX];
  //Represents the index of the current item being parsed in whatever array on the stack
  uint32_t stack_array_current_indices[UINT8_MAX] = {0};
  uint32_t stack_array_sizes[UINT8_MAX] = {0};
  int32_t current_array_stack_index = -1;

  auto update_current_array_stack_index = [&]() constexpr noexcept{
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

  auto count_items_in_array = [&](int32_t open_square_symbol_index) constexpr noexcept{
    int32_t item_count = 0;
    uint32_t extra_thingy = 0;

    for(auto symbol_index = open_square_symbol_index + 1; symbol_index < symbol_count; ++symbol_index){
      auto current_symbol = symbols[symbol_index];
      if(extra_thingy){
        switch(current_symbol){
          case Symbol::close_squigily:
          case Symbol::close_square: 
            --extra_thingy; continue;
          default: continue;
        }

      }else switch(current_symbol){
        case Symbol::open_square:
        case Symbol::open_squigily:
          ++extra_thingy; continue;
        case Symbol::comma: ++item_count; continue;
        case Symbol::close_square: ++item_count; return item_count;
        default: continue;
      }
    }

    return item_count;
  };

  auto add_array_to_stack_and_init_array_in_gltf_object = [&](int32_t open_square_symbol_index) constexpr noexcept{
    //TODO: calculate array size.
    uint32_t array_size = count_items_in_array(open_square_symbol_index);
    ++stack_depth;
    stack_items[stack_depth] = array;
    stack_array_sizes[stack_depth] = array_size;
    stack_array_current_indices[stack_depth] = 0;

    switch(current_gltf_item){
      case glTF_Item::scenes: {
        gltf.scenes = allocator(sizeof(GLTF::Scene) * array_size); return;
      };
    }
  };

  auto get_value_string_from_value_definition = [&](int32_t end_of_definition_symbol_index) constexpr noexcept{
    auto begin_offset = symbol_offsets[end_of_definition_symbol_index-1] + 1; 
    auto end_offset = symbol_offsets[end_of_definition_symbol_index] - 1;
    return String{
      .data = json + begin_offset,
      .length = end_offset - begin_offset,
    };
  };

  auto set_value_in_glTF = [&](String value, bool is_in_array = false) constexpr noexcept{

    if(is_in_array and stack_items[stack_depth -1] not_eq definition) return Error::problem;
    if(not is_in_array and stack_items[stack_depth] not_eq definition) return Error::problem;

    auto check_definition_is = [&](const char * tag) constexpr noexcept{
      auto current_definition = stack_tags[stack_depth - is_in_array];
      return memcmp(tag, current_definition.data, current_definition.length);
    };

    //This always assumes correct data.
    auto value_as_int = [&] constexpr noexcept{
      uint32_t int_value = 0;
      for(auto i = 0; i < value.length; ++i){
        int_value += (value.data[i] - '0') * (10 * (value.length - i));
      }
      return int_value;
    };

    if(current_gltf_item == glTF_Item::asset){
      if(check_definition_is("generator")) gltf.asset.generator = value;
      else if (check_definition_is("version")) gltf.asset.version = value;
      else return Error::problem;
      return Error::no_problem;
    }

    //Only dealing with array objects from this point on
    if(stack_items[2] not_eq array) return Error::problem;
    auto current_index = stack_array_current_indices[2];

    Parse_Item expected_stack_pattern[UINT8_MAX] = {none};
    auto expected_stack_depth = 0;
    auto add_item_to_pattern = [&](Parse_Item item) constexpr noexcept{
      expected_stack_pattern[expected_stack_depth] = item;
      ++expected_stack_depth;
      return expected_stack_depth;
    };

    auto compair_stacks = [&] constexpr noexcept{
      return memcmp(stack_items, expected_stack_pattern, expected_stack_depth * sizeof(Parse_Item));
    };
    //Base pattern
    add_item_to_pattern(root);
    add_item_to_pattern(definition);
    add_item_to_pattern(array);
    add_item_to_pattern(object);

    auto root_item_definition_stack_index = 
      add_item_to_pattern(definition);
    auto root_item_tag = stack_tags[root_item_definition_stack_index];

    auto set_meshes_primitives = [&]{
      if(not compair(root_item_tag, "primitives")) return;

      auto primitive_array_stack_index = 
        add_item_to_pattern(array);
      add_item_to_pattern(object);
      add_item_to_pattern(definition);

      if(memcmp(stack_items, expected_stack_pattern, 8 * sizeof(Parse_Item)))
        auto definition = stack_tags[expected_stack_depth];
        auto primitives_index = stack_array_current_indices[primitive_array_stack_index];
        auto & primitive = gltf.meshes[current_index].primitives[primitives_index];
        auto int_value = value_as_int();
        if(compair(definition, "attributes")){
          auto & attributes = primitive.attributes;
               if(check_definition_is("POSITION")) attributes.POSITION = int_value;
          else if(check_definition_is("JOINTS_0")) attributes.JOINTS_0 = int_value;
          else if(check_definition_is("TEXCOORD_0")) attributes.TEXCOORD_0 = int_value;
          else if(check_definition_is("WEIGHTS_0")) attributes.WEIGHTS_0 = int_value;
          else if(check_definition_is("NORMAL")) attributes.NORMAL = int_value;
        }
        else if(check_definition_is("indices")) primitive.indices = int_value;
        else if(check_definition_is("material")) primitive.material = int_value;
        else if(check_definition_is("mode")) primitive.mode = int_value;
        return Error::no_problem;
    };



    switch(current_gltf_item){
      case glTF_Item::scenes:
        if(check_definition_is("name")) gltf.scenes[current_index].name = value;
        else if(check_definition_is("nodes")){
          auto nodes_index = stack_array_current_indices[stack_depth];
          gltf.scenes[current_index].nodes[nodes_index] = value_as_int();
        }
        return Error::no_problem;
      case glTF_Item::nodes:
        if(check_definition_is("name")) gltf.nodes[current_index].name = value;
        //else if(check_definition_is("mesh")) gltf.nodes[current_index].
        return Error::no_problem;
      case glTF_Item::meshes:
        if(check_definition_is("name")) gltf.meshes[current_index].name = value;
        else return set_meshes_primitives();
        return Error::no_problem;
      case glTF_Item::accessors:
        if(check_definition_is("bufferView")); 
      case glTF_Item::buffer_views:
      case glTF_Item::buffers:
        return Error::no_problem;
    };
    return Error::no_problem;
  };

  auto add_defintion_to_stack = [&](String definition) constexpr noexcept{
    ++stack_depth;
    stack_items[stack_depth] = definition;
    stack_tags[stack_depth] = definition;
  };

  auto add_object_to_stack = [&]{
    ++stack_depth;
    stack_items[stack_depth] = object;
  };

  auto add_glTF_root_item_to_stack = [&](String string) constexpr noexcept{
    if(memcmp(string.data, "asset", string.length)){
      add_defintion_to_stack(string);
      current_gltf_item = glTF_Item::asset;
    }
  };

  auto check_is_actualy_value_definition = [&](int32_t end_of_definition_symbol_index) constexpr noexcept{
    auto previous_symbol = symbols[end_of_definition_symbol_index-1];
    if(previous_symbol not_eq Symbol::open_square and previous_symbol not_eq Symbol::comma){
      return false;
    }
    return true;
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
      case root :{ 
        if(has_string) 
          add_glTF_root_item_to_stack(string); 
        //Should be imposible to get hear unless the file is malformed.
        else return Error::problem;
        continue;
      }

      case definition :{ 
        if(has_string) set_value_in_glTF(string);
        else switch(current_symbol){
          case Symbol::open_squigily: add_object_to_stack(); continue;
          case Symbol::open_square: add_array_to_stack_and_init_array_in_gltf_object(symbol_index); continue;
          //must be a value
          case Symbol::close_squigily:{
            auto begin_offset = symbol_offsets[symbol_index-1] + 1; 
            auto end_offset = symbol_offsets[symbol_index] - 1;
            auto string =  String{
              .data = json + begin_offset,
              .length = end_offset - begin_offset,
            };

            set_value_in_glTF(value);
            //Close out the object.
            --stack_depth;
            continue;
          }
          case Symbol::comma:{
            auto value = get_value_string_from_value_definition(symbol_index);
            set_value_in_glTF(value);
            continue;
          }
          default:continue;
        }
        continue;
      }

      case object:{
        if(has_string) add_defintion_to_stack(string); 
        else if(current_symbol == Symbol::close_squigily) --stack_depth;
        continue;
      }

      case array:{ 
        if(has_string){
          set_value_in_glTF(string);
          continue;
        }else switch(current_symbol){
          case Symbol::open_squigily: add_object_to_stack(); continue;
          //There are no arrays of arrays in the gltf spec.
          case Symbol::open_square: return Error::problem;

          case Symbol::comma:{
            auto is_value = check_is_actualy_value_definition(symbol_index);
            if(is_value){
              auto value = get_value_string_from_value_definition(symbol_index);
              set_value_in_glTF_array(value);
            }
          }

          case Symbol::close_square:{
            auto is_value = check_is_actualy_value_definition(symbol_index);
            if(is_value){
              auto value = get_value_string_from_value_definition(symbol_index);
              set_value_in_glTF_value_array(value);
            }
            --stack_depth;
            continue;
          }
          
          default: continue;
        }
        continue;
      }
    }
  }

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
