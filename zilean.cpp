#include "zilean.h"
#include "../plugin_sdk/plugin_sdk.hpp"
#include <string>
#include "utils.h"
#include "permashow.hpp"
#include <algorithm>
#include <vector>
#include <unordered_set>

namespace zilean
{
    constexpr float Q_RANGE = 900.0f;
    constexpr float W_RANGE = 700.0f;
    constexpr float E_RANGE = 550.0f;
    constexpr float R_RANGE = 900.0f;
    constexpr float Q_RADIUS = 150.0f;

    static script_spell* q = nullptr;
    static script_spell* w = nullptr;
    static script_spell* e = nullptr;
    static script_spell* r = nullptr;

    static uint32_t combo_target_id = 0;
    static float combo_q1_cast_time = 0.0f;
    static bool waiting_for_qwq = false;

    // --- QWQ FARM STATE ---
    static bool waiting_for_farm_qwq = false;
    static float farm_q1_cast_time = 0.0f;
    static game_object_script farm_qwq_target = nullptr;

    namespace settings
    {
        TreeTab* main_tab = nullptr;
        TreeEntry* auto_q = nullptr;
        TreeEntry* q_double_bomb = nullptr;
        TreeEntry* auto_w = nullptr;
        TreeEntry* auto_e = nullptr;
        TreeEntry* e_mode = nullptr;
        TreeEntry* auto_r = nullptr;
        TreeEntry* r_min_hp = nullptr;
        TreeEntry* r_save_allies = nullptr;
        TreeEntry* farm_q = nullptr;
        TreeEntry* farm_hotkey = nullptr;
        TreeEntry* farm_permashow = nullptr;
        TreeEntry* farm_w = nullptr;
        TreeEntry* farm_qwq_min = nullptr;
        TreeEntry* draw_range_q = nullptr;
        TreeEntry* draw_range_e = nullptr;
        TreeEntry* draw_range_r = nullptr;
        TreeEntry* q_hitchance = nullptr;
        TreeEntry* safe_q_range_slider = nullptr;
        TreeEntry* r_priority_list = nullptr;
        TreeEntry* e_priority_list = nullptr;
        TreeEntry* anti_melee = nullptr;
        TreeEntry* e_speed_hotkey = nullptr;
        TreeEntry* e_slow_hotkey = nullptr;
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

    bool has_zilean_bomb(const game_object_script& obj)
    {
        if (!obj || !obj->is_valid()) return false;
        for (const auto& buff : obj->get_bufflist())
        {
            if (buff && buff->is_valid())
            {
                std::string name = buff->get_name();
                if (name.find("ZileanQEnemy") != std::string::npos ||
                    name.find("ZileanQAttach") != std::string::npos ||
                    name.find("ZileanQBuff") != std::string::npos)
                    return true;
            }
        }
        return false;
    }

    float get_safe_q_range()
    {
        int percent = settings::safe_q_range_slider ? settings::safe_q_range_slider->get_int() : 70;
        percent = std::max(30, std::min(percent, 100));
        return Q_RANGE * (percent / 100.f);
    }

    bool e_is_on_self()
    {
        if (!myhero) return false;
        for (const auto& buff : myhero->get_bufflist())
        {
            if (buff && buff->is_valid())
            {
                const std::string& n = buff->get_name();
                if (n.find("ZileanE") != std::string::npos)
                    return true;
            }
        }
        return false;
    }

    game_object_script get_best_ally_for(TreeEntry* prio_list, const std::vector<game_object_script>& allies)
    {
        std::vector<std::pair<game_object_script, int>> valid;
        for (auto& a : allies)
        {
            auto pr = prio_list->get_prority(a->get_network_id());
            if (pr.first == -1 || !pr.second) continue;
            valid.push_back({ a, pr.first });
        }
        std::sort(valid.begin(), valid.end(), [](auto& a, auto& b) { return a.second < b.second; });
        return valid.empty() ? nullptr : valid.front().first;
    }

    static const std::vector<std::string> channel_ult_buffs = {
        "shenstandunitedlockout",
        "karthusfallenonetarget",
        "kayler",
        "fioraw",
        "malzaharpassiveshield"
    };
    static const std::vector<std::string> bomb_on_stasis_buffs = {
        "zhonyasringshield",
        "bardrstasis",
        "guardianangel",
        "yonepassivesoul"
    };

    bool has_channel_ult_buff(game_object_script target)
    {
        if (!target) return false;
        for (const auto& buff : target->get_bufflist())
        {
            if (buff && buff->is_valid())
            {
                std::string b = buff->get_name();
                for (const auto& dangerous : channel_ult_buffs)
                {
                    if (b.find(dangerous) != std::string::npos)
                        return true;
                }
            }
        }
        return false;
    }

    bool is_target_in_stasis_or_revive(game_object_script target)
    {
        if (!target) return false;
        for (const auto& buff : target->get_bufflist())
        {
            if (!buff || !buff->is_valid()) continue;
            std::string name = buff->get_name();
            for (const auto& special : bomb_on_stasis_buffs)
            {
                if (name.find(special) != std::string::npos)
                    return true;
            }
        }
        return false;
    }

    bool is_gapclose_spell(const spell_instance_script& active_spell)
    {
        if (active_spell && active_spell->get_spell_data() &&
            std::string(active_spell->get_spell_data()->get_name()).find("TristanaW") != std::string::npos)
            return true;
        return false;
    }

    void zilean_combo()
    {
        if (!myhero || myhero->is_dead()) return;
        auto target = target_selector->get_target(Q_RANGE, damage_type::magical);
        float safe_q_range = get_safe_q_range();
        float now = gametime->get_time();

        if (!target || !target->is_valid() || target->is_dead() || !target->is_visible())
        {
            waiting_for_qwq = false;
            return;
        }
        float dist = myhero->get_distance(target);
        hit_chance hc = get_hitchance_by_config(settings::q_hitchance);

        if (!waiting_for_qwq)
        {
            bool do_double_bomb = (settings::q_double_bomb && settings::q_double_bomb->get_bool())
                                || has_channel_ult_buff(target)
                                || is_target_in_stasis_or_revive(target);

            if (q && q->is_ready() && dist < safe_q_range)
            {
                if (q->cast(target, hc))
                {
                    combo_target_id = target->get_network_id();
                    combo_q1_cast_time = now;
                    waiting_for_qwq = do_double_bomb;
                    return;
                }
            }
            if (e && e->is_ready() && dist > safe_q_range - 100)
            {
                int e_mode = settings::e_mode ? settings::e_mode->get_int() : 0;
                if (e_mode == 0)
                {
                    std::vector<game_object_script> allies = entitylist->get_ally_heroes();
                    auto best_e = get_best_ally_for(settings::e_priority_list, allies);
                    if (best_e && best_e->is_me() && !e_is_on_self())
                        e->cast(best_e);
                }
            }
            return;
        }

        if (waiting_for_qwq && target->get_network_id() == combo_target_id)
        {
            if (now - combo_q1_cast_time > 1.8f)
            {
                waiting_for_qwq = false;
                return;
            }
            if (w && w->is_ready())
            {
                w->cast();
                return;
            }
            if (q && q->is_ready())
            {
                if (q->cast(target, hc))
                {
                    waiting_for_qwq = false;
                    return;
                }
            }
            if (e && e->is_ready())
            {
                int e_mode = settings::e_mode ? settings::e_mode->get_int() : 1;
                if (e_mode == 1 && dist <= E_RANGE && !target->has_buff_type(buff_type::Slow))
                    e->cast(target);
            }
            return;
        }
        waiting_for_qwq = false;
    }

    void zilean_flee()
    {
        if (e && e->is_ready() && myhero && !myhero->is_dead())
        {
            if (!e_is_on_self())
                e->cast(myhero);
        }
    }

    void farm_with_q()
    {
        if (!settings::farm_q || !settings::farm_q->get_bool()) return;
        if (!q || !q->is_ready()) return;
        if (settings::farm_hotkey && !settings::farm_hotkey->get_bool()) return;

        auto minions = entitylist->get_enemy_minions();
        if (minions.empty()) return;

        int best_count = 0;
        game_object_script best_minion = nullptr;
        for (auto& m : minions)
        {
            if (!m || !m->is_valid() || m->is_dead() || m->get_distance(myhero) > Q_RANGE)
                continue;

            int count = 0;
            for (auto& n : minions)
            {
                if (!n || n->is_dead() || n->get_distance(m) > Q_RADIUS)
                    continue;
                count++;
            }
            if (count > best_count)
            {
                best_count = count;
                best_minion = m;
            }
        }

        int min_minions_for_qwq = settings::farm_qwq_min ? settings::farm_qwq_min->get_int() : 6;
        bool use_w_in_farm = settings::farm_w ? settings::farm_w->get_bool() : true;

        if (!waiting_for_farm_qwq && use_w_in_farm && best_minion && best_count >= min_minions_for_qwq && w && w->is_ready())
        {
            if (q->cast(best_minion))
            {
                waiting_for_farm_qwq = true;
                farm_qwq_target = best_minion;
                farm_q1_cast_time = gametime->get_time();
            }
            return;
        }
        if (best_minion && !waiting_for_farm_qwq)
        {
            q->cast(best_minion);
        }
    }

    void try_cast_r()
    {
        if (!(settings::auto_r && settings::auto_r->get_bool() && r && r->is_ready())) return;

        int min_hp = settings::r_min_hp ? settings::r_min_hp->get_int() : 15; // lowered from 20 to 15
        bool save_allies = settings::r_save_allies ? settings::r_save_allies->get_bool() : true;

        auto is_in_danger = [](game_object_script hero) -> bool {
            if (!hero || hero->is_dead()) return false;
            for (auto& buff : hero->get_bufflist())
            {
                if (!buff || !buff->is_valid()) continue;
                std::string n = buff->get_name();
                if (n.find("ZileanR") != std::string::npos || n.find("ChronoRevive") != std::string::npos)
                    return false;
            }
            for (auto& turret : entitylist->get_enemy_turrets())
            {
                if (turret && turret->is_valid() && !turret->is_dead() && turret->get_distance(hero) < 775)
                    if (hero->get_health_percent() < 30) return true;
            }
            for (auto& buff : hero->get_bufflist())
            {
                if (!buff || !buff->is_valid()) continue;
                std::string name = buff->get_name();
                if (name.find("summonerdot") != std::string::npos
                    || name.find("ignite") != std::string::npos
                    || name.find("burn") != std::string::npos
                    || name.find("deathmark") != std::string::npos
                    || name.find("mordekaiserchildrenofthegrave") != std::string::npos
                    || name.find("brandablaze") != std::string::npos
                    || name.find("cassiopeiapoisontarget") != std::string::npos
                    || name.find("fizzmarinerdoombomb") != std::string::npos)
                    return true;
            }
            int enemy_count = 0;
            for (auto& enemy : entitylist->get_enemy_heroes())
            {
                if (enemy && enemy->is_valid() && !enemy->is_dead() && enemy->is_visible() && enemy->get_distance(hero) < 750)
                    enemy_count++;
            }
            if (enemy_count >= 2 && hero->get_health_percent() < 40) return true;
            return false;
        };

        if (settings::r_priority_list)
        {
            std::vector<game_object_script> all_candidates = entitylist->get_ally_heroes();
            all_candidates.push_back(myhero);

            std::sort(all_candidates.begin(), all_candidates.end(), [&](const game_object_script& a, const game_object_script& b) {
                auto pa = settings::r_priority_list->get_prority(a->get_network_id());
                auto pb = settings::r_priority_list->get_prority(b->get_network_id());
                return pa.first < pb.first;
            });

            for (auto& ally : all_candidates)
            {
                if (!ally || ally->is_dead() || myhero->get_distance(ally) > R_RANGE) continue;
                auto pr = settings::r_priority_list->get_prority(ally->get_network_id());
                if (pr.first == -1 || !pr.second) continue;
                bool already_reviving = false;
                for (auto& buff : ally->get_bufflist())
                {
                    if (!buff || !buff->is_valid()) continue;
                    std::string n = buff->get_name();
                    if (n.find("ZileanR") != std::string::npos || n.find("ChronoRevive") != std::string::npos)
                        already_reviving = true;
                }
                if (already_reviving) continue;
                if (ally->get_health_percent() < min_hp || is_in_danger(ally))
                {
                    r->cast(ally);
                    return;
                }
            }
        }
    }

    void anti_melee_logic()
    {
        if (!(settings::anti_melee && settings::anti_melee->get_bool())) return;
        if (!e || !e->is_ready() || !myhero || myhero->is_dead()) return;
        for (auto& enemy : entitylist->get_enemy_heroes())
        {
            if (enemy && enemy->is_valid() && !enemy->is_dead() && enemy->is_visible()
                && enemy->is_ai_hero() && enemy->get_distance(myhero) < 400
                && enemy->get_attack_range() < 250)
            {
                if (!e_is_on_self())
                {
                    e->cast(myhero);
                    break;
                }
            }
        }
    }

    void e_speed_hotkey_logic()
    {
        if (!e || !e->is_ready()) return;
        if (!myhero || myhero->is_dead()) return;
        if (!e_is_on_self())
        {
            e->cast(myhero);
            return;
        }
        if (settings::e_priority_list)
        {
            std::vector<game_object_script> allies = entitylist->get_ally_heroes();
            auto best = get_best_ally_for(settings::e_priority_list, allies);
            if (best && best->is_valid() && !best->is_dead() && best->get_distance(myhero) <= E_RANGE && !best->is_me())
                e->cast(best);
        }
    }

    void e_slow_hotkey_logic()
    {
        if (!e || !e->is_ready()) return;
        if (!myhero || myhero->is_dead()) return;
        game_object_script closest = nullptr;
        float closest_dist = 9999.0f;
        for (auto& enemy : entitylist->get_enemy_heroes())
        {
            if (!enemy || !enemy->is_valid() || enemy->is_dead() || !enemy->is_visible())
                continue;
            float dist = myhero->get_distance(enemy);
            if (dist <= E_RANGE && dist < closest_dist)
            {
                closest = enemy;
                closest_dist = dist;
            }
        }
        if (closest)
            e->cast(closest);
    }

    void on_update()
    {
        if (!myhero || myhero->is_dead()) return;

        if (waiting_for_farm_qwq)
        {
            if (!farm_qwq_target || !farm_qwq_target->is_valid() || farm_qwq_target->is_dead())
            {
                waiting_for_farm_qwq = false;
                farm_qwq_target = nullptr;
            }
            else
            {
                float now = gametime->get_time();
                if (w && w->is_ready())
                {
                    w->cast();
                }
                if (q && q->is_ready())
                {
                    q->cast(farm_qwq_target);
                    waiting_for_farm_qwq = false;
                    farm_qwq_target = nullptr;
                }
                if (now - farm_q1_cast_time > 2.0f)
                {
                    waiting_for_farm_qwq = false;
                    farm_qwq_target = nullptr;
                }
            }
        }

        if (settings::e_speed_hotkey && settings::e_speed_hotkey->get_bool())
            e_speed_hotkey_logic();
        if (settings::e_slow_hotkey && settings::e_slow_hotkey->get_bool())
            e_slow_hotkey_logic();

        for (auto& enemy : entitylist->get_enemy_heroes())
        {
            if (enemy && enemy->is_valid() && !enemy->is_dead() && enemy->is_visible() && enemy->is_ai_hero())
            {
                auto active_spell = enemy->get_active_spell();
                if (active_spell && is_gapclose_spell(active_spell))
                {
                    if (e && e->is_ready() && myhero->get_distance(enemy) <= E_RANGE)
                        e->cast(enemy);
                    if (q && q->is_ready() && myhero->get_distance(enemy) <= Q_RANGE)
                        q->cast(enemy);
                    if (w && w->is_ready())
                        w->cast();
                    if (q && q->is_ready() && myhero->get_distance(enemy) <= Q_RANGE)
                        q->cast(enemy);
                    break;
                }
            }
        }

        if (orbwalker->flee_mode())
        {
            zilean_flee();
            return;
        }

        anti_melee_logic();

        if (orbwalker->combo_mode())
        {
            zilean_combo();
            try_cast_r();
        }
        else if (orbwalker->lane_clear_mode() || orbwalker->last_hit_mode())
        {
            farm_with_q();
        }
        else
        {
            waiting_for_qwq = false;
        }
    }

    void on_draw()
    {
        if (!myhero) return;
        auto pos = myhero->get_position();

        if (settings::draw_range_q && settings::draw_range_q->get_bool())
            draw_manager->add_circle(pos, Q_RANGE, D3DCOLOR_ARGB(120, 50, 200, 255));
        if (settings::draw_range_e && settings::draw_range_e->get_bool())
            draw_manager->add_circle(pos, E_RANGE, D3DCOLOR_ARGB(120, 120, 255, 0));
        if (settings::draw_range_r && settings::draw_range_r->get_bool())
            draw_manager->add_circle(pos, R_RANGE, D3DCOLOR_ARGB(80, 255, 50, 50));
        if (settings::safe_q_range_slider && settings::draw_range_q && settings::draw_range_q->get_bool())
            draw_manager->add_circle(pos, get_safe_q_range(), D3DCOLOR_ARGB(80, 100, 255, 200));

        vector draw_pos = pos;
        draw_pos.y += 30.0f;
        if (settings::farm_hotkey)
        {
            bool enabled = settings::farm_hotkey->get_bool();
            auto color = enabled ? D3DCOLOR_ARGB(255, 30, 255, 30) : D3DCOLOR_ARGB(255, 255, 30, 30);
            draw_manager->add_text(
                draw_pos,
                color,
                16,
                enabled ? "Farm Q: ON" : "Farm Q: OFF"
            );
        }
    }

    void load()
    {
        q = plugin_sdk->register_spell(spellslot::q, Q_RANGE);
        w = plugin_sdk->register_spell(spellslot::w, W_RANGE);
        e = plugin_sdk->register_spell(spellslot::e, E_RANGE);
        r = plugin_sdk->register_spell(spellslot::r, R_RANGE);

        if (!q || !w || !e || !r)
        {
            console->print("Zilean spell init failed!");
            return;
        }

        q->set_skillshot(0.28f, 150.0f, 1100.0f, {}, skillshot_type::skillshot_circle);

        settings::main_tab = menu->create_tab("carry.zilean", "Zilean");
        auto main = settings::main_tab->add_tab("carry.zilean.main", "Main");
        settings::auto_q = main->add_checkbox("carry.zilean.main.q", "Auto Q in combo", true);
        settings::q_double_bomb = main->add_checkbox("carry.zilean.q.doublebomb", "Try Double Bomb (Q twice)", true);
        settings::auto_w = main->add_checkbox("carry.zilean.main.w", "Auto W (for quick bomb combo)", true);
        settings::auto_e = main->add_checkbox("carry.zilean.main.e", "Auto E (Speed/Slow)", true);
        settings::e_mode = main->add_combobox(
            "carry.zilean.e.mode", "E Mode",
            { {"Speed Ally", nullptr}, {"Slow Enemy", nullptr} },
            0
        );
        settings::e_speed_hotkey = main->add_hotkey(
            "carry.zilean.e.speedhotkey", "E Speed (self/ally) Hotkey", TreeHotkeyMode::Hold, 0x43, false);
        settings::e_slow_hotkey = main->add_hotkey(
            "carry.zilean.e.slowhotkey", "E Slow Enemy Hotkey", TreeHotkeyMode::Hold, 0x56, false);
        settings::auto_r = main->add_checkbox("carry.zilean.main.r", "Auto R (save self/allies)", true);
        settings::r_min_hp = main->add_slider("carry.zilean.r.minhp", "Ult at X% HP", 15, 1, 70); // Default is now 15
        settings::r_save_allies = main->add_checkbox("carry.zilean.r.saveallies", "Auto Ult Allies", true);
        settings::q_hitchance = main->add_combobox("carry.zilean.q.hitchance", "Q Hitchance", { {"Low",nullptr}, {"Medium",nullptr}, {"High",nullptr}, {"Very High",nullptr} }, 2);
        settings::safe_q_range_slider = main->add_slider(
            "carry.zilean.q.saferange", "Safe Q Range (%)", 70, 30, 100);
        settings::anti_melee = main->add_checkbox("carry.zilean.anti_melee", "Anti-melee E self", true);

        auto farm = settings::main_tab->add_tab("carry.zilean.farm", "Farm");
        settings::farm_q = farm->add_checkbox("carry.zilean.farm.q", "Farm Q", false);
        settings::farm_hotkey = farm->add_hotkey("carry.zilean.farm.hotkey", "Farm Q Hotkey", TreeHotkeyMode::Toggle, 0x58, true);
        settings::farm_permashow = farm->add_checkbox("carry.zilean.farm.permashow", "Show Farm Q permashow", true);
        settings::farm_w = farm->add_checkbox("carry.zilean.farm.w", "Use W in QWQ farm", true);
        settings::farm_qwq_min = farm->add_slider("carry.zilean.farm.qwqmin", "Min minions for QWQ", 6, 2, 12);

        auto draw = settings::main_tab->add_tab("carry.zilean.draw", "Drawings");
        settings::draw_range_q = draw->add_checkbox("carry.zilean.draw.q", "Draw Q Range", true);
        settings::draw_range_e = draw->add_checkbox("carry.zilean.draw.e", "Draw E Range", false);
        settings::draw_range_r = draw->add_checkbox("carry.zilean.draw.r", "Draw R Range", false);

        auto r_prio_tab = settings::main_tab->add_tab("carry.zilean.rprio", "R Priority List");
        auto e_prio_tab = settings::main_tab->add_tab("carry.zilean.eprio", "E Priority List");

        std::vector<ProrityCheckItem> ally_prio_items;
        for (auto&& ally : entitylist->get_ally_heroes())
        {
            if (!ally || !ally->is_valid()) continue;
            ally_prio_items.push_back({ ally->get_network_id(), ally->get_name(), true, ally->get_square_icon_portrait() });
        }
        settings::r_priority_list = r_prio_tab->add_prority_list("carry.zilean.rprio", "R Priority", ally_prio_items, false, false);
        settings::e_priority_list = e_prio_tab->add_prority_list("carry.zilean.eprio", "E Priority", ally_prio_items, false, false);

        event_handler<events::on_update>::add_callback(on_update);
        event_handler<events::on_draw>::add_callback(on_draw);

        Permashow::Instance.Init(settings::main_tab, "Zilean");
        Permashow::Instance.AddElement("Farm Q", settings::farm_hotkey);
        Permashow::Instance.AddElement("E Mode", settings::e_mode);
        Permashow::Instance.AddElement("E Speed", settings::e_speed_hotkey);
        Permashow::Instance.AddElement("E Slow", settings::e_slow_hotkey);

        console->print("Zilean plugin loaded!");
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
        console->print("Zilean plugin unloaded!");
    }
}
