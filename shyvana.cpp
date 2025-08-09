#include "shyvana.h"
#include "../plugin_sdk/plugin_sdk.hpp"
#include "utils.h"
#include "permashow.hpp"
#include <vector>
#include <string>
#include <algorithm>

namespace shyvana
{
    constexpr float E_RANGE = 925.0f;
    constexpr float E_SPEED = 1600.0f;
    constexpr float E_SPEED_DRAGON = 1575.0f;
    constexpr float E_WIDTH = 120.0f;
    constexpr float E_CAST_TIME = 0.25f;
    constexpr float E_CAST_TIME_DRAGON = 0.3333f;
    constexpr float E_RADIUS_DRAGON = 345.0f;

    // How far behind the enemy to aim (tweak as needed)
    constexpr float E_LEAD_DISTANCE = 400.0f;

    static script_spell* e = nullptr;
    static TreeTab* main_tab = nullptr;

    namespace settings
    {
        TreeEntry* use_e_combo = nullptr;
        TreeEntry* use_e_harass = nullptr;
        TreeEntry* use_e_laneclear = nullptr;
        TreeEntry* use_e_jungle = nullptr;
        TreeEntry* minions_hit = nullptr;
        TreeEntry* e_hitchance = nullptr;
    }

    hit_chance get_hitchance_by_config(TreeEntry* hit)
    {
        switch (hit ? hit->get_int() : 2)
        {
            case 0: return hit_chance::low;
            case 1: return hit_chance::medium;
            case 2: return hit_chance::high;
            case 3: return hit_chance::very_high;
            default: return hit_chance::high;
        }
    }

    bool in_dragon_form()
    {
        return myhero->has_buff(buff_hash("ShyvanaTransform"));
    }

    float get_e_speed()
    {
        return in_dragon_form() ? E_SPEED_DRAGON : E_SPEED;
    }

    float get_e_width()
    {
        return in_dragon_form() ? E_RADIUS_DRAGON : E_WIDTH;
    }

    float get_e_casttime()
    {
        return in_dragon_form() ? E_CAST_TIME_DRAGON : E_CAST_TIME;
    }

    void update_e_params()
    {
        e->set_skillshot(
            get_e_casttime(),
            get_e_width(),
            get_e_speed(),
            { collisionable_objects::minions, collisionable_objects::heroes },
            skillshot_type::skillshot_line
        );
    }

    vector get_e_aim_position(game_object_script target)
    {
        vector from = myhero->get_position();
        vector to = target->get_position();
        vector dir = target->get_pathing_direction();

        // If they are running, dir is not zero, use it.
        if (dir.length() > 0.1f)
        {
            // Predict a spot behind the target, toward their movement
            vector cast_pos = to + dir.normalized() * E_LEAD_DISTANCE;

            // Clamp to max range from player
            if (from.distance(cast_pos) > E_RANGE)
            {
                cast_pos = from + (cast_pos - from).normalized() * E_RANGE;
            }

            return cast_pos;
        }
        // If not moving, just cast at them
        return to;
    }

    void combo()
    {
        if (!settings::use_e_combo->get_bool() || !e->is_ready())
            return;

        auto target = target_selector->get_target(E_RANGE, damage_type::magical);
        if (target && target->is_valid_target(E_RANGE))
        {
            update_e_params();

            // Use custom aim position if moving, otherwise use prediction
            vector cast_pos = get_e_aim_position(target);

            e->cast(cast_pos);
        }
    }

    void harass()
    {
        if (!settings::use_e_harass->get_bool() || !e->is_ready())
            return;

        for (auto& target : entitylist->get_enemy_heroes())
        {
            if (target && target->is_valid_target(E_RANGE))
            {
                update_e_params();
                vector cast_pos = get_e_aim_position(target);
                e->cast(cast_pos);
                break;
            }
        }
    }

    // uwu

    void laneclear()
    {
        if (!settings::use_e_laneclear->get_bool() || !e->is_ready())
            return;

        auto minions = entitylist->get_enemy_minions();
        std::sort(minions.begin(), minions.end(), [](game_object_script a, game_object_script b)
        {
            return a->get_position().distance(myhero->get_position()) < b->get_position().distance(myhero->get_position());
        });

        int required = settings::minions_hit->get_int();

        for (auto& minion : minions)
        {
            if (!minion || !minion->is_valid() || minion->is_dead() || !minion->is_valid_target(E_RANGE))
                continue;

            auto pred = e->get_prediction(minion);
            if (!pred._cast_position.is_valid())
                continue;

            int count = 0;
            for (auto& m : minions)
            {
                if (!m || !m->is_valid() || m->is_dead())
                    continue;
                if (pred._cast_position.distance(m->get_position()) < get_e_width())
                    count++;
            }

            if (count >= required)
            {
                update_e_params();
                e->cast(pred._cast_position);
                break;
            }
        }
    }

    void jungleclear()
    {
        if (!settings::use_e_jungle->get_bool() || !e->is_ready())
            return;

        auto monsters = entitylist->get_jugnle_mobs_minions();
        std::sort(monsters.begin(), monsters.end(), [](game_object_script a, game_object_script b)
        {
            return a->get_max_health() > b->get_max_health();
        });

        for (auto& mob : monsters)
        {
            if (!mob || !mob->is_valid() || mob->is_dead() || mob->is_ward() || !mob->is_valid_target(E_RANGE))
                continue;
            if (mob->get_health() < 200.0f)
                continue;

            update_e_params();
            e->cast(mob->get_position());
            break;
        }
    }

    void on_update()
    {
        if (orbwalker->combo_mode())
            combo();
        if (orbwalker->harass())
            harass();
        if (orbwalker->lane_clear_mode())
        {
            laneclear();
            jungleclear();
        }
    }

    void load()
    {
        e = plugin_sdk->register_spell(spellslot::e, E_RANGE);

        main_tab = menu->create_tab("carry.shyvana", "Shyvana");

        auto main = main_tab->add_tab("carry.shyvana.main", "Main");

        // Hotkeys - X (0x58) by default
        settings::use_e_combo = main->add_hotkey("carry.shyvana.main.e.combo", "Use E in Combo (Toggle)", TreeHotkeyMode::Toggle, 0x58, true);
        settings::use_e_harass = main->add_hotkey("carry.shyvana.main.e.harass", "Use E in Harass (Toggle)", TreeHotkeyMode::Toggle, 0x58, true);
        settings::use_e_laneclear = main->add_hotkey("carry.shyvana.main.e.laneclear", "Use E in Lane Clear (Toggle)", TreeHotkeyMode::Toggle, 0x58, true);
        settings::use_e_jungle = main->add_hotkey("carry.shyvana.main.e.jungle", "Use E in Jungle Clear (Toggle)", TreeHotkeyMode::Toggle, 0x58, true);
        settings::minions_hit = main->add_slider("carry.shyvana.main.minionshit", "Min Minions for E (Lane)", 3, 1, 6);
        settings::e_hitchance = main->add_combobox("carry.shyvana.main.ehitchance", "E Hitchance", { {"Low",nullptr}, {"Medium",nullptr}, {"High",nullptr}, {"Very High",nullptr} }, 2);

        // Permashow
        Permashow::Instance.Init(main_tab, "Shyvana");
        Permashow::Instance.AddElement("E Combo", settings::use_e_combo);
        Permashow::Instance.AddElement("E Harass", settings::use_e_harass);
        Permashow::Instance.AddElement("E LaneClear", settings::use_e_laneclear);
        Permashow::Instance.AddElement("E Jungle", settings::use_e_jungle);
        Permashow::Instance.AddElement("Minions for E", settings::minions_hit);
        Permashow::Instance.AddElement("E Hitchance", settings::e_hitchance);

        event_handler<events::on_update>::add_callback(on_update);

        console->print("Shyvana plugin loaded!");
    }

    void unload()
    {
        if (main_tab) menu->delete_tab(main_tab);
        if (e) plugin_sdk->remove_spell(e);
        event_handler<events::on_update>::remove_handler(on_update);
        console->print("Shyvana plugin unloaded!");
    }
}
