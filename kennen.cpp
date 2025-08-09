#include "kennen.h"
#include "../plugin_sdk/plugin_sdk.hpp"
#include <string>
#include "utils.h"
#include "permashow.hpp"
#include <algorithm>
#include <vector>

namespace kennen
{
    constexpr float Q_RANGE_DEFAULT = 1050.0f;
    constexpr float Q_SPEED = 1700.0f;
    constexpr float Q_WIDTH = 70.0f;
    constexpr float Q_DELAY = 0.175f;
    constexpr float W_RANGE = 750.0f;
    constexpr float R_RANGE = 550.0f;
    constexpr float Q_FARM_RADIUS = 140.0f;
    constexpr float E_RANGE = 750.0f;

    static script_spell* q = nullptr;
    static script_spell* w = nullptr;
    static script_spell* e = nullptr;
    static script_spell* r = nullptr;

    namespace settings
    {
        TreeTab* main_tab = nullptr;
        // Combo
        TreeEntry* use_q = nullptr;
        TreeEntry* q_hitchance = nullptr;
        TreeEntry* use_w = nullptr;
        TreeEntry* auto_w_3stacks = nullptr;
        TreeEntry* use_e_combo = nullptr;
        TreeEntry* use_r = nullptr;
        TreeEntry* r_enemy_slider = nullptr;
        // Harass
        TreeEntry* use_q_harass = nullptr;
        TreeEntry* use_w_harass = nullptr;
        TreeEntry* use_e_harass = nullptr;
        TreeEntry* harass_mana = nullptr;
        // Farm/LaneClear
        TreeEntry* farm_q = nullptr;
        TreeEntry* farm_q_range = nullptr;
        TreeEntry* farm_w = nullptr;
        TreeEntry* farm_e = nullptr;
        TreeEntry* farm_min_q = nullptr;
        TreeEntry* farm_mana = nullptr;
        // E modes
        TreeEntry* use_e_flee = nullptr;
        TreeEntry* use_e_tower = nullptr;
        TreeEntry* use_e_tower_hotkey = nullptr; // MB4 toggle
        // Drawing
        TreeEntry* draw_range_q = nullptr;
        TreeEntry* draw_range_w = nullptr;
        TreeEntry* draw_range_e = nullptr;
        TreeEntry* draw_range_r = nullptr;
        // Killsteal
        TreeTab* ks_tab = nullptr;
        TreeEntry* ks_q = nullptr;
        TreeEntry* ks_w = nullptr;
        TreeEntry* ks_e = nullptr;
        TreeEntry* ks_r = nullptr;
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

    bool has_three_stacks(const game_object_script& obj)
    {
        if (!obj || !obj->is_valid()) return false;
        for (const auto& buff : obj->get_bufflist())
        {
            if (buff && buff->is_valid())
            {
                std::string name = buff->get_name();
                if (name.find("kennenmarkofstorm") != std::string::npos && buff->get_count() >= 3)
                    return true;
            }
        }
        return false;
    }

    bool has_two_stacks(const game_object_script& obj)
    {
        if (!obj || !obj->is_valid()) return false;
        for (const auto& buff : obj->get_bufflist())
        {
            if (buff && buff->is_valid())
            {
                std::string name = buff->get_name();
                if (name.find("kennenmarkofstorm") != std::string::npos && buff->get_count() == 2)
                    return true;
            }
        }
        return false;
    }

    bool has_any_stacks(const game_object_script& obj)
    {
        if (!obj || !obj->is_valid()) return false;
        for (const auto& buff : obj->get_bufflist())
        {
            if (buff && buff->is_valid())
            {
                std::string name = buff->get_name();
                if (name.find("kennenmarkofstorm") != std::string::npos && buff->get_count() >= 1)
                    return true;
            }
        }
        return false;
    }

    int get_kennen_stack_count(const game_object_script& obj)
    {
        if (!obj || !obj->is_valid()) return 0;
        for (const auto& buff : obj->get_bufflist())
        {
            if (buff && buff->is_valid())
            {
                std::string name = buff->get_name();
                if (name.find("kennenmarkofstorm") != std::string::npos)
                    return buff->get_count();
            }
        }
        return 0;
    }

    bool kennen_w_passive_ready()
    {
        if (!myhero) return false;
        for (const auto& buff : myhero->get_bufflist())
        {
            if (buff && buff->is_valid())
            {
                std::string name = buff->get_name();
                if (name.find("kennenwdummyproc") != std::string::npos)
                    return true;
            }
        }
        return false;
    }

    bool is_near_enemy_turret()
    {
        for (auto& turret : entitylist->get_enemy_turrets())
        {
            if (turret && turret->is_valid() && !turret->is_dead() &&
                turret->get_distance(myhero) < 900.0f)
                return true;
        }
        return false;
    }

    static int kennen_stacks = 0;
    static float last_stack_time = 0.0f;
    static float stack_duration = 6.0f;

    void update_stacks(const game_object_script& target)
    {
        if (!target || !target->is_valid()) return;
        kennen_stacks = get_kennen_stack_count(target);
        last_stack_time = gametime->get_time();
    }

    void kennen_cast_q()
    {
        auto target = target_selector->get_target(Q_RANGE_DEFAULT, damage_type::magical);
        if (target && target->is_valid() && !target->is_dead() && target->is_visible())
        {
            q->cast(target, get_hitchance_by_config(settings::q_hitchance));
        }
    }

    void kennen_auto_w()
    {
        if (!w || !w->is_ready()) return;
        for (auto& enemy : entitylist->get_enemy_heroes())
        {
            if (enemy && enemy->is_valid() && !enemy->is_dead() && enemy->is_visible() && myhero->get_distance(enemy) < W_RANGE)
            {
                if (settings::auto_w_3stacks && settings::auto_w_3stacks->get_bool())
                {
                    if (has_three_stacks(enemy))
                    {
                        w->cast();
                        return;
                    }
                }
                else if (has_two_stacks(enemy))
                {
                    w->cast();
                    return;
                }
            }
        }
    }

    void kennen_e_logic()
    {
        if (!e || !e->is_ready() || !myhero || myhero->is_dead()) return;

        if (settings::use_e_flee && settings::use_e_flee->get_bool() && orbwalker->flee_mode())
        {
            e->cast();
            return;
        }

        if (settings::use_e_tower && settings::use_e_tower->get_bool() &&
            settings::use_e_tower_hotkey && settings::use_e_tower_hotkey->get_bool() &&
            is_near_enemy_turret())
        {
            e->cast();
            return;
        }

        if (settings::use_e_combo && settings::use_e_combo->get_bool() && orbwalker->combo_mode())
        {
            for (auto& enemy : entitylist->get_enemy_heroes())
            {
                if (enemy && enemy->is_valid() && !enemy->is_dead() && enemy->is_visible() && myhero->get_distance(enemy) < E_RANGE)
                {
                    int stacks = get_kennen_stack_count(enemy);
                    if (stacks == 2)
                    {
                        e->cast();
                        return;
                    }
                    e->cast();
                    return;
                }
            }
        }
    }

    void kennen_r_logic()
    {
        if (!r || !r->is_ready() || !myhero || myhero->is_dead()) return;
        if (!settings::use_r || !settings::use_r->get_bool()) return;

        int min_enemies = settings::r_enemy_slider ? settings::r_enemy_slider->get_int() : 3;
        int count = 0;
        for (auto& enemy : entitylist->get_enemy_heroes())
        {
            if (enemy && enemy->is_valid() && !enemy->is_dead() && enemy->is_visible() && myhero->get_distance(enemy) < R_RANGE)
                count++;
        }
        if (count >= min_enemies)
        {
            r->cast();
        }
    }

    void kennen_farm_q()
    {
        if (!settings::farm_q || !settings::farm_q->get_bool()) return;
        if (!q || !q->is_ready()) return;
        if (myhero->get_mana_percent() < (settings::farm_mana ? settings::farm_mana->get_int() : 40)) return;

        auto minions = entitylist->get_enemy_minions();
        if (minions.empty()) return;

        float farm_range = settings::farm_q_range ? static_cast<float>(settings::farm_q_range->get_int()) : Q_RANGE_DEFAULT;
        int min_for_q = settings::farm_min_q ? settings::farm_min_q->get_int() : 3;
        int best_count = 0;
        vector best_position;

        for (auto& m : minions)
        {
            if (!m || !m->is_valid() || m->is_dead() || m->get_distance(myhero) > farm_range)
                continue;

            auto pred = q->get_prediction(m);
            if (!pred._cast_position.is_valid()) continue;

            int count = 0;
            for (auto& n : minions)
            {
                if (!n || n->is_dead()) continue;
                if (pred._cast_position.distance(n->get_position()) < Q_FARM_RADIUS)
                    count++;
            }
            if (count > best_count)
            {
                best_count = count;
                best_position = pred._cast_position;
            }
        }

        if (best_count >= min_for_q && best_position.is_valid())
        {
            q->cast(best_position);
        }
    }

    void kennen_farm_w()
    {
        if (!settings::farm_w || !settings::farm_w->get_bool()) return;
        if (!w || !w->is_ready()) return;
        if (myhero->get_mana_percent() < (settings::farm_mana ? settings::farm_mana->get_int() : 40)) return;

        auto minions = entitylist->get_enemy_minions();
        int count = 0;
        for (auto& m : minions)
        {
            if (!m || !m->is_valid() || m->is_dead() || myhero->get_distance(m) > W_RANGE)
                continue;

            for (auto& buff : m->get_bufflist())
            {
                if (buff && buff->is_valid())
                {
                    std::string name = buff->get_name();
                    if (name.find("kennenmarkofstorm") != std::string::npos)
                    {
                        count++;
                        break;
                    }
                }
            }
        }
        if (count >= (settings::farm_min_q ? settings::farm_min_q->get_int() : 3))
            w->cast();
    }

    void kennen_farm_e()
    {
        if (!settings::farm_e || !settings::farm_e->get_bool()) return;
        if (!e || !e->is_ready()) return;
        if (myhero->get_mana_percent() < (settings::farm_mana ? settings::farm_mana->get_int() : 40)) return;

        int min_for_e = settings::farm_min_q ? settings::farm_min_q->get_int() : 3;
        auto minions = entitylist->get_enemy_minions();
        int count = 0;
        for (auto& m : minions)
        {
            if (!m || !m->is_valid() || m->is_dead() || myhero->get_distance(m) > 325.0f)
                continue;
            count++;
        }
        if (count >= min_for_e)
            e->cast();
    }

    void kennen_harass()
    {
        if (!myhero || myhero->is_dead()) return;
        if (myhero->get_mana_percent() < (settings::harass_mana ? settings::harass_mana->get_int() : 40)) return;

        if (settings::use_q_harass && settings::use_q_harass->get_bool() && q && q->is_ready())
        {
            auto target = target_selector->get_target(Q_RANGE_DEFAULT, damage_type::magical);
            if (target && target->is_valid() && !target->is_dead() && target->is_visible())
                q->cast(target, get_hitchance_by_config(settings::q_hitchance));
        }
        if (settings::use_w_harass && settings::use_w_harass->get_bool() && w && w->is_ready())
        {
            for (auto& enemy : entitylist->get_enemy_heroes())
            {
                if (enemy && enemy->is_valid() && !enemy->is_dead() && enemy->is_visible() && myhero->get_distance(enemy) < W_RANGE)
                {
                    if (has_two_stacks(enemy))
                    {
                        w->cast();
                        break;
                    }
                }
            }
        }
        if (settings::use_e_harass && settings::use_e_harass->get_bool() && e && e->is_ready())
        {
            for (auto& enemy : entitylist->get_enemy_heroes())
            {
                if (enemy && enemy->is_valid() && !enemy->is_dead() && enemy->is_visible() && myhero->get_distance(enemy) < 700.0f)
                {
                    e->cast();
                    break;
                }
            }
        }
    }

    void kennen_killsteal()
    {
        if (!myhero || myhero->is_dead()) return;

        // Q killsteal
        if (settings::ks_q && settings::ks_q->get_bool() && q && q->is_ready())
        {
            for (auto& enemy : entitylist->get_enemy_heroes())
            {
                if (!enemy || !enemy->is_valid() || enemy->is_dead() || !enemy->is_visible()) continue;
                if (myhero->get_distance(enemy) > Q_RANGE_DEFAULT) continue;
                double dmg = q->get_damage(enemy);
                if (dmg >= enemy->get_health())
                {
                    q->cast(enemy, hit_chance::medium);
                    return;
                }
            }
        }

        // W killsteal (cast if enemy has any stack)
        if (settings::ks_w && settings::ks_w->get_bool() && w && w->is_ready())
        {
            for (auto& enemy : entitylist->get_enemy_heroes())
            {
                if (!enemy || !enemy->is_valid() || enemy->is_dead() || !enemy->is_visible()) continue;
                if (myhero->get_distance(enemy) > W_RANGE) continue;
                if (!has_any_stacks(enemy)) continue;
                double dmg = w->get_damage(enemy);
                if (dmg >= enemy->get_health())
                {
                    w->cast();
                    return;
                }
            }
        }

        // E killsteal
        if (settings::ks_e && settings::ks_e->get_bool() && e && e->is_ready())
        {
            for (auto& enemy : entitylist->get_enemy_heroes())
            {
                if (!enemy || !enemy->is_valid() || enemy->is_dead() || !enemy->is_visible()) continue;
                if (myhero->get_distance(enemy) > E_RANGE) continue;
                double dmg = e->get_damage(enemy);
                if (dmg >= enemy->get_health())
                {
                    e->cast();
                    return;
                }
            }
        }

        // R killsteal
        if (settings::ks_r && settings::ks_r->get_bool() && r && r->is_ready())
        {
            for (auto& enemy : entitylist->get_enemy_heroes())
            {
                if (!enemy || !enemy->is_valid() || enemy->is_dead() || !enemy->is_visible()) continue;
                if (myhero->get_distance(enemy) > R_RANGE) continue;
                double dmg = r->get_damage(enemy);
                if (dmg >= enemy->get_health())
                {
                    r->cast();
                    return;
                }
            }
        }
    }

    void on_update()
    {
        if (!myhero || myhero->is_dead()) return;

        kennen_killsteal();

        kennen_e_logic();

        if (orbwalker->combo_mode())
        {
            if (settings::use_q && settings::use_q->get_bool() && q && q->is_ready())
                kennen_cast_q();

            if (settings::use_w && settings::use_w->get_bool() && w && w->is_ready())
                kennen_auto_w();

            kennen_r_logic();
        }
        else if (orbwalker->harass())
        {
            kennen_harass();
        }
        else if (orbwalker->lane_clear_mode() || orbwalker->last_hit_mode())
        {
            kennen_farm_q();
            kennen_farm_w();
            kennen_farm_e();
        }
    }

    void on_draw()
    {
        if (!myhero) return;
        auto pos = myhero->get_position();

        if (settings::draw_range_q && settings::draw_range_q->get_bool())
            draw_manager->add_circle(pos, Q_RANGE_DEFAULT, D3DCOLOR_ARGB(120, 50, 200, 255));
        if (settings::draw_range_w && settings::draw_range_w->get_bool())
            draw_manager->add_circle(pos, W_RANGE, D3DCOLOR_ARGB(120, 255, 255, 0));
        if (settings::draw_range_e && settings::draw_range_e->get_bool())
            draw_manager->add_circle(pos, 700.0f, D3DCOLOR_ARGB(120, 100, 255, 200));
        if (settings::draw_range_r && settings::draw_range_r->get_bool())
            draw_manager->add_circle(pos, R_RANGE, D3DCOLOR_ARGB(120, 255, 50, 50));

        if (settings::farm_q_range && (orbwalker->lane_clear_mode() || orbwalker->last_hit_mode()))
        {
            int range = settings::farm_q_range->get_int();
            draw_manager->add_circle(pos, static_cast<float>(range), D3DCOLOR_ARGB(80, 80, 180, 255));
        }
    }

    void load()
    {
        q = plugin_sdk->register_spell(spellslot::q, Q_RANGE_DEFAULT);
        w = plugin_sdk->register_spell(spellslot::w, W_RANGE);
        e = plugin_sdk->register_spell(spellslot::e, 0.0f);
        r = plugin_sdk->register_spell(spellslot::r, R_RANGE);

        if (!q || !w || !e || !r)
        {
            console->print("Kennen spell init failed!");
            return;
        }
        q->set_skillshot(Q_DELAY, Q_WIDTH, Q_SPEED, { collisionable_objects::minions, collisionable_objects::heroes }, skillshot_type::skillshot_line);

        settings::main_tab = menu->create_tab("carry.kennen", "Kennen");
        auto main = settings::main_tab->add_tab("carry.kennen.main", "Main");
        // Combo
        settings::use_q = main->add_checkbox("carry.kennen.main.q", "Use Q (Combo)", true);
        settings::q_hitchance = main->add_combobox("carry.kennen.q.hitchance", "Q Hitchance", { {"Low",nullptr}, {"Medium",nullptr}, {"High",nullptr}, {"Very High",nullptr} }, 2);
        settings::use_w = main->add_checkbox("carry.kennen.main.w", "Use W (Combo)", true);
        settings::auto_w_3stacks = main->add_checkbox("carry.kennen.main.w.auto", "Auto W at 3 stacks only", false);
        settings::use_e_combo = main->add_checkbox("carry.kennen.main.e.combo", "Use E to engage (Combo)", true);
        settings::use_r = main->add_checkbox("carry.kennen.main.r", "Auto R if X enemies", true);
        settings::r_enemy_slider = main->add_slider("carry.kennen.main.r.slider", "Auto R min enemies", 3, 2, 5);
        // Harass
        auto harass = settings::main_tab->add_tab("carry.kennen.harass", "Harass");
        settings::use_q_harass = harass->add_checkbox("carry.kennen.harass.q", "Use Q", true);
        settings::use_w_harass = harass->add_checkbox("carry.kennen.harass.w", "Use W (stun on 2 stacks)", true);
        settings::use_e_harass = harass->add_checkbox("carry.kennen.harass.e", "Use E", false);
        settings::harass_mana = harass->add_slider("carry.kennen.harass.mana", "Harass: Min Mana %", 40, 1, 100);
        // Farm/LaneClear
        auto farm = settings::main_tab->add_tab("carry.kennen.farm", "Farm");
        settings::farm_q = farm->add_checkbox("carry.kennen.farm.q", "Use Q to farm", true);
        settings::farm_q_range = farm->add_slider("carry.kennen.farm.qrange", "Q Farm Range", 900, 400, static_cast<int>(Q_RANGE_DEFAULT));
        settings::farm_w = farm->add_checkbox("carry.kennen.farm.w", "Use W to farm", false);
        settings::farm_e = farm->add_checkbox("carry.kennen.farm.e", "Use E to farm", false);
        settings::farm_min_q = farm->add_slider("carry.kennen.farm.qmin", "Min minions to use Q/W/E", 3, 1, 7);
        settings::farm_mana = farm->add_slider("carry.kennen.farm.mana", "Farm: Min Mana %", 40, 1, 100);
        // E Flee/Tower
        settings::use_e_flee = main->add_checkbox("carry.kennen.main.e.flee", "Use E to Flee (Flee key)", true);
        settings::use_e_tower = main->add_checkbox("carry.kennen.main.e.tower", "Use E for Tower Push", true);
        settings::use_e_tower_hotkey = main->add_hotkey("carry.kennen.main.e.tower.hotkey", "E Tower Push Toggle", TreeHotkeyMode::Toggle, 0x05, false); // MB4
        // Drawing
        auto draw = settings::main_tab->add_tab("carry.kennen.draw", "Drawings");
        settings::draw_range_q = draw->add_checkbox("carry.kennen.draw.q", "Draw Q Range", true);
        settings::draw_range_w = draw->add_checkbox("carry.kennen.draw.w", "Draw W Range", false);
        settings::draw_range_e = draw->add_checkbox("carry.kennen.draw.e", "Draw E Range", false);
        settings::draw_range_r = draw->add_checkbox("carry.kennen.draw.r", "Draw R Range", true);

        // Killsteal tab
        settings::ks_tab = settings::main_tab->add_tab("carry.kennen.ks", "Killsteal");
        settings::ks_q = settings::ks_tab->add_checkbox("carry.kennen.ks.q", "Use Q for Killsteal", true);
        settings::ks_w = settings::ks_tab->add_checkbox("carry.kennen.ks.w", "Use W for Killsteal", true);
        settings::ks_e = settings::ks_tab->add_checkbox("carry.kennen.ks.e", "Use E for Killsteal", true);
        settings::ks_r = settings::ks_tab->add_checkbox("carry.kennen.ks.r", "Use R for Killsteal", false);

        event_handler<events::on_update>::add_callback(on_update);
        event_handler<events::on_draw>::add_callback(on_draw);

        // Permashow setup for Kennen
        Permashow::Instance.Init(settings::main_tab, "Kennen");
        Permashow::Instance.AddElement("Farm Q", settings::farm_q);
        Permashow::Instance.AddElement("Farm W", settings::farm_w);
        Permashow::Instance.AddElement("E Tower Push", settings::use_e_tower_hotkey); // MB4 toggle state

        console->print("Kennen plugin loaded!");
    }

    void unload()
    {
        if (settings::main_tab) menu->delete_tab(settings::main_tab);
        if (q) plugin_sdk->remove_spell(q);
        if (w) plugin_sdk->remove_spell(w);
        if (e) plugin_sdk->remove_spell(e);
        if (r) plugin_sdk->remove_spell(r);
        event_handler<events::on_update>::remove_handler(on_update);
        event_handler<events::on_draw>::remove_handler(on_draw);
        console->print("Kennen plugin unloaded!");
    }
} 
