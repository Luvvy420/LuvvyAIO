#include "../plugin_sdk/plugin_sdk.hpp"

#pragma once
namespace utils
{
	// line add
	int count_enemy_minions_in_range(float range, const vector& pos);

	// AIO utilities
	//
	void on_load();

	// Buff checks
	//
	bool has_unkillable_buff(game_object_script target);
	bool has_untargetable_buff(game_object_script target);
	bool has_crowd_control_buff(game_object_script target);

	// Spell casting
	//
	bool fast_cast(script_spell* spell);
	bool cast(spellslot slot, bool is_charged_spell);
	bool cast(spellslot slot, game_object_script unit, bool is_charged_spell);
	bool fast_cast(script_spell* spell, vector position);
	bool cast(spellslot slot, vector position, bool is_charged_spell);
	bool fast_cast(script_spell* spell, game_object_script unit, hit_chance minimum, bool aoe, int min_targets);
	bool fast_cast(script_spell* spell, int minMinions, bool is_jugnle_mobs);

	// Spell utilities
	//
	bool is_ready(spellslot slot);
	
	// Other
	//
	bool enabled_in_map(std::map<std::uint32_t, TreeEntry*> map, game_object_script target);
};

