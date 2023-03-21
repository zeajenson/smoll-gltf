#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstddef>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>
#include <memory_resource>

namespace smoll{

constexpr uint32_t glTF = 0x46546C67;
//Chunk Types
constexpr uint32_t JSON = 0x4E4F534A;
//Probably wont support because blender doesnt output this.
constexpr uint32_t BIN = 0x004E4942;

using byte = uint8_t;

enum class Error: int32_t{
  no_problem=0,
  problem = 1,
  not_glTF = -1
};

struct GLTF{

  //Non Owning.
  byte const * data;
  uint32_t length;
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

enum class Token_Type{
  open_square,
  close_square,
  open_squigily,
  close_squigily,

  asset,
  scenes,
  meshes,
  accessors,
  buffer_views,
  buffers,


};

struct Token{

};

constexpr Error parse_gltf(byte const * const raw, GLTF * gltf, auto allocator) noexcept{
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
  json_chunk.data = raw + 16;

  Chunk binary_chunk;
  memcpy(&binary_chunk.length, raw + 16 + json_chunk.length, 4);
  memcpy(&binary_chunk.type, raw + 16 + json_chunk.length + 4, 4);
  if(json_chunk.type != BIN){
    return Error::problem;
  }
  //TODO tokinize and stuff 

  std::pmr::vector<Token_Type> tokens; 
  tokens.reserve(json_chunk.length);

  return Error::no_problem;
}

}
