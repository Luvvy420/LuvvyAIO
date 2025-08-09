#include "../plugin_sdk/plugin_sdk.hpp"
#include "utils.h"
#include <map>

namespace utils
{
    // Declaration of menu objects
    TreeTab* main_tab = nullptr;

    // Delays map
    std::map<spellslot, float> delays;

    namespace developer
    {
        TreeEntry* debug_mode = nullptr;
    }

    // Minion count utility (the new function)
    int count_enemy_minions_in_range(float range, const vector& pos)
    {
        int count = 0;
        for (auto&& minion : entitylist->get_enemy_minions())
        {
            if (minion && minion->is_valid_target(range, pos))
                ++count;
        }
        return count;
    }

    void on_load()
    {
        std::string data = myhero->get_model();
        std::transform(data.begin(), data.end(), data.begin(),
            [](unsigned char c) { return std::tolower(c); });

        main_tab = menu->get_tab(data);
        if (main_tab != nullptr)
        {
            auto developer = main_tab->add_tab(myhero->get_model() + ".developer", "Developer Settings");
            {
                developer->add_separator(myhero->get_model() + ".aio", "Luvvy : meowlista build 1");
                developer::debug_mode = developer->add_checkbox(myhero->get_model() + ".developer.debug_mode", "Debug Mode", false);
            }
        }

        std::string msg = "<b><font color=\"#7289da\">[luvvy]</font></b><font color=\"#FFFFFF\">: luvvy'd champion</font> <b><font color=\"#7289da\">" + myhero->get_model() + "</font></b>";
        myhero->print_chat(1, msg.c_str());
    }

    bool has_unkillable_buff(game_object_script target)
    {
        return target->is_zombie() || target->has_buff({ buff_hash("UndyingRage"), buff_hash("ChronoShift"), buff_hash("KayleR"), buff_hash("KindredRNoDeathBuff") });
    }

    bool has_untargetable_buff(game_object_script target)
    {
        return target->has_buff({ buff_hash("JaxCounterStrike"), buff_hash("ShenW") });
    }

    bool has_crowd_control_buff(game_object_script target)
    {
        return target->has_buff_type({
            buff_type::Stun,
            buff_type::Knockup,
            buff_type::Asleep,
            buff_type::Berserk,
            buff_type::Charm,
            buff_type::Flee,
            buff_type::Fear,
            buff_type::Snare,
            buff_type::Suppression,
            buff_type::Polymorph,
            buff_type::Taunt
        });
    }

    bool is_empowered()
    {
        return myhero->get_mana() >= 4.0f;
    }

    bool fast_cast(script_spell* spell)
    {
        if (main_tab != nullptr && developer::debug_mode->get_bool())
        {
            myhero->print_chat(1, "[luvvyAIO Debug] Fast Cast (spell) %s ready: %s state: %d empowered: %s", spell->name().c_str(), spell->is_ready() ? "Yes" : "No", spell->toogle_state(), is_empowered() ? "Yes" : "No");
        }
        myhero->cast_spell(spell->slot, true, spell->is_charged_spell);
        return true;
    }

    bool cast(spellslot slot, bool is_charged_spell)
    {
        if (gametime->get_time() < delays[slot] + sciprt_spell_wait)
            return false;

        myhero->cast_spell(slot, true, is_charged_spell);
        delays[slot] = gametime->get_time();
        return true;
    }

    bool cast(spellslot slot, game_object_script unit, bool is_charged_spell)
    {
        if (gametime->get_time() < delays[slot] + sciprt_spell_wait)
            return false;

        myhero->cast_spell(slot, unit, true, is_charged_spell);
        delays[slot] = gametime->get_time();
        return true;
    }

    bool fast_cast(script_spell* spell, vector position)
    {
        if (main_tab != nullptr && developer::debug_mode->get_bool())
        {
            myhero->print_chat(1, "[LuvvyAIO Debug] Fast Cast (spell, position) %s ready: %s state: %d empowered: %s", spell->name().c_str(), spell->is_ready() ? "Yes" : "No", spell->toogle_state(), is_empowered() ? "Yes" : "No");
        }
        if (!spell->is_charged_spell)
        {
            myhero->cast_spell(spell->slot, position);
            return true;
        }
        if (spell->is_charging() && gametime->get_time() - spell->charging_started_time > 0.f)
        {
            myhero->update_charged_spell(spell->slot, position, true);
            return true;
        }
        return spell->start_charging();
    }

    bool cast(spellslot slot, vector position, bool is_charged_spell)
    {
        if (gametime->get_time() < delays[slot] + sciprt_spell_wait)
            return false;

        myhero->cast_spell(slot, position, true, is_charged_spell);

        delays[slot] = gametime->get_time();
        return true;
    }

    bool fast_cast(script_spell* spell, game_object_script unit, hit_chance minimum, bool aoe, int min_targets)
    {
        if (main_tab != nullptr && developer::debug_mode->get_bool())
        {
            myhero->print_chat(1, "[LuvvyAIO Debug] Fast Cast (spell, unit) %s unit: %s ready: %s state: %d empowered: %s", spell->name().c_str(), unit->get_name_cstr(), spell->is_ready() ? "Yes" : "No", spell->toogle_state(), is_empowered() ? "Yes" : "No");
        }

        vector cast_position;

        prediction_input x;

        if (!spell->from.is_valid())
            x._from = myhero->get_position();
        else
            x._from = spell->from;

        x.unit = unit;
        x.delay = spell->delay;
        x.radius = spell->radius;
        x.speed = spell->speed;
        x.collision_objects = spell->collision_flags;
        x.range = spell->range();
        x.type = spell->type;
        x.aoe = aoe;
        x.spell_slot = spell->slot;
        x.use_bounding_radius = spell->type != skillshot_type::skillshot_circle;

        auto output = prediction->get_prediction(&x);

        if (output.hitchance >= minimum && output.aoe_targets_hit_count() >= min_targets)
        {
            cast_position = output.get_cast_position();

            if (!spell->is_charged_spell)
            {
                myhero->cast_spell(spell->slot, cast_position);
                return true;
            }

            if (spell->is_charging() && gametime->get_time() - spell->charging_started_time > 0.f)
            {
                myhero->update_charged_spell(spell->slot, cast_position, true);
                return true;
            }
        }
        return false;
    }

    bool fast_cast(script_spell* spell, int minMinions, bool is_jugnle_mobs)
    {
        auto best_pos = spell->get_cast_on_best_farm_position(minMinions, is_jugnle_mobs);

        if (best_pos.is_valid())
        {
            if (!spell->is_charged_spell)
            {
                if (developer::debug_mode->get_bool())
                {
                    myhero->print_chat(1, "[LuvvyAIO Debug] Fast Cast (spell, farm) %s ready: %s state: %d", spell->name().c_str(), spell->is_ready() ? "Yes" : "No", spell->toogle_state());
                }
                myhero->cast_spell(spell->slot, best_pos);
                return true;
            }

            if (spell->is_charging() && gametime->get_time() - spell->charging_started_time > 0.f)
            {
                myhero->update_charged_spell(spell->slot, best_pos, true);
                return true;
            }
        }

        return false;
    }

    bool is_ready(spellslot slot)
    {
        auto spellInfo = myhero->get_spell(slot);

        if (spellInfo && spellInfo->is_learned())
        {
            auto spell_state = myhero->get_spell_state(slot);

            if (spell_state == 2)
            {
                return true;
            }
        }

        return false;
    }

    bool enabled_in_map(std::map<std::uint32_t, TreeEntry*> map, game_object_script target)
    {
        auto it = map.find(target->get_network_id());
        if (it == map.end())
            return false;

        return it->second->get_bool();
    }
}
