#pragma once
namespace cenisyspe::block {
// Register every concrete block into the Block::get() flyweight map.
// Call once at server startup before any chunk send.
void register_all_blocks();
} // namespace
