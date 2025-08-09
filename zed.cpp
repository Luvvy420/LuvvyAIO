#include "../plugin_sdk/plugin_sdk.hpp"
#include "zed.h"
#include "utils.h"
#include "permashow.hpp"
#include <vector>
#include <algorithm>
#include <string>
#include <map>

namespace zed
{
    // --- Spells ---
    script_spell* q = nullptr;    // Zed's Q (shuriken)
    script_spell* w = nullptr;    // Zed's W (shadow)
    script_spell* e = nullptr;    // Zed's E (slash)
    script_spell* r = nullptr;    // Zed's R (ultimate)
    script_spell* flash = nullptr;// Flash summoner spell

    // --- Menu Tabs and Entries ---
    TreeTab* main_tab = nullptr;

    // Combo
    TreeEntry* allin_key = nullptr;
    TreeEntry* combo_save_q_for_r = nullptr;
    TreeEntry* combo_q_hold_range = nullptr;
    TreeEntry* combo_swap_back_delay = nullptr;
    TreeEntry* manual_w_cooldown = nullptr; // Cooldown for manual W when auto cast OFF

    // Harass
    TreeEntry* harass_key = nullptr;
    TreeEntry* harass_use_q = nullptr;
    TreeEntry* harass_min_mana = nullptr;

    // Farming
    TreeEntry* spellfarm_key = nullptr;
    TreeEntry* farm_use_q = nullptr;
    TreeEntry* farm_use_w = nullptr;
    TreeEntry* farm_use_e = nullptr;
    TreeEntry* jungle_use_q = nullptr;
    TreeEntry* jungle_use_w = nullptr;
    TreeEntry* jungle_use_e = nullptr;

    // Killsteal
    TreeEntry* killsteal_q = nullptr;
    TreeEntry* killsteal_e = nullptr;
    TreeEntry* killsteal_r = nullptr;

    // Draw
    TreeEntry* draw_q_range = nullptr;
    TreeEntry* draw_w_range = nullptr;
    TreeEntry* draw_e_range = nullptr;
    TreeEntry* draw_r_range = nullptr;

    // Toggle for W2 auto cast (shadow swap)
    TreeEntry* w2_auto_cast_toggle = nullptr;
    bool allow_w2_cast = true; // Updated every frame from permashow toggle

    // R blacklist per enemy champ to disable R usage
    std::map<std::string, TreeEntry*> r_blacklist_entries;

    // ShadowRunner buff hash for shadow detection
    static const uint32_t SHADOWRUNNER_BUFF_HASH = 0xD5F12997;

    // --- Helper: Get shadow position by scanning allies for ShadowRunner buff ---
    vector get_shadow_position()
    {
        if (!myhero) return vector(0, 0, 0);

        for (auto& ally : entitylist->get_ally_heroes())
        {
            if (!ally || !ally->is_valid() || ally->is_dead())
                continue;

            // Check buffs via get_bufflist()
            auto buffs = ally->get_bufflist();
            for (auto& buff : buffs)
            {
                if (!buff || !buff->is_valid() || buff->get_hash_name() != SHADOWRUNNER_BUFF_HASH)
                    continue;

                // Return position of ally with ShadowRunner buff (the shadow)
                return ally->get_position();
            }
        }

        return vector(0, 0, 0);
    }

    // --- Damage Calculation Helpers ---
    float get_q_damage(game_object_script target)
    {
        float base = 150.0f;
        float armor = target->get_armor();
        float reduction = 100.0f / (100.0f + armor);
        return base * reduction;
    }

    float get_e_damage(game_object_script target)
    {
        float base = 100.0f;
        float armor = target->get_armor();
        float reduction = 100.0f / (100.0f + armor);
        return base * reduction;
    }

    float get_auto_attack_damage(game_object_script target)
    {
        return myhero->get_total_attack_damage() - target->get_armor() * 0.5f;
    }

    float get_r_damage(game_object_script target)
    {
        float missing = target->get_max_health() - target->get_health();
        return missing * 0.25f;
    }

    // --- Checks if using too much damage (overkill) ---
    bool is_overkill(game_object_script target)
    {
        if (!target || !target->is_valid()) return false;

        float health = target->get_health();
        float qd = get_q_damage(target);
        float ed = get_e_damage(target);
        float aa = get_auto_attack_damage(target);

        if (target->is_valid_target(myhero->get_attack_range()))
        {
            if (qd >= health) return true;
            if (qd + ed >= health) return true;
            if (qd + ed + aa >= health) return true;
            if (aa * 2 >= health) return true;
        }
        else
        {
            if (qd + ed >= health) return true;
            if (qd >= health) return true;
        }
        return false;
    }

    // --- Checks if we can use R on this target (not blacklisted) ---
    bool can_use_r_on(game_object_script target)
    {
        if (!target || !target->is_valid()) return false;

        std::string name = target->get_model();
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);

        auto it = r_blacklist_entries.find(name);
        if (it != r_blacklist_entries.end() && it->second && !it->second->get_bool())
            return false;

        return true;
    }

    // --- Dangerous ult detection to auto-cast R defensively ---
    bool is_dangerous_spell(spellslot slot, const std::string& caster_name)
    {
        static std::set<std::string> dangerous_casters = {
            "fizz", "diana", "maokai", "morgana", "malphite", "veigar", "vi",
            "leona", "riven", "warwick", "nocturne", "nautilus"
        };
        static std::set<spellslot> dangerous_slots = { spellslot::r };
        return dangerous_casters.count(caster_name) && dangerous_slots.count(slot);
    }

    void on_process_spell_cast(game_object_script sender, spell_instance_script spell)
    {
        if (!sender || sender->is_ally()) return;

        std::string caster = sender->get_model();
        std::transform(caster.begin(), caster.end(), caster.begin(), ::tolower);
        spellslot slot = spell->get_spellslot();

        if (is_dangerous_spell(slot, caster) && r->is_ready())
        {
            auto evade_target = target_selector->get_target(r->range(), damage_type::physical);
            if (evade_target && evade_target->is_valid_target(r->range()))
                r->cast(evade_target);
        }
    }

    // --- Combo Logic: Simple All-in only ---
    void combo_simple_allin(game_object_script target)
    {
        bool save_q_for_r = combo_save_q_for_r && combo_save_q_for_r->get_bool();
        int q_hold_range = combo_q_hold_range ? combo_q_hold_range->get_int() : 400;
        int swap_back_delay_ticks = combo_swap_back_delay ? combo_swap_back_delay->get_int() : 7;
        float swap_back_delay = swap_back_delay_ticks * 0.1f;

        static float last_combo_time = 0;
        static float last_w_cast_time = 0;  // For W cooldown when toggle OFF
        static bool can_swap_back = false;

        float now = gametime->get_time();
        bool shadow_deployed = myhero->has_buff(SHADOWRUNNER_BUFF_HASH);

        // 1) Cast R if ready and allowed on target
        if (r->is_ready() && target->is_valid_target(r->range()) && can_use_r_on(target) && !is_overkill(target))
        {
            r->cast(target);
            last_combo_time = now;
            can_swap_back = false;
        }

        bool w_toggle_on = w2_auto_cast_toggle && w2_auto_cast_toggle->get_bool();
        float manual_w_cd = manual_w_cooldown ? manual_w_cooldown->get_int() : 5;

        // 2) Cast W (shadow) if toggle ON, or toggle OFF but cooldown elapsed
        if (w->is_ready() && !shadow_deployed && target->is_valid_target(w->range()))
        {
            if (w_toggle_on)
            {
                // Auto W toggle ON: cast normally
                w->cast(target->get_position());
                last_w_cast_time = now;
            }
            else
            {
                // Auto W toggle OFF: only cast if cooldown passed
                if (now - last_w_cast_time > manual_w_cd)
                {
                    w->cast(target->get_position());
                    last_w_cast_time = now;
                }
            }
        }

        // 3) Cast E slash if ready and target in range of either hero or shadow
        if (e->is_ready())
        {
            auto shadow_pos = get_shadow_position();
            bool target_in_range = target->is_valid_target(e->range()) ||
            (shadow_pos != vector(0, 0, 0) && target->get_position().distance(shadow_pos) <= e->range());


            if (target_in_range)
            {
                e->cast();
            }
        }

        // 4) Cast Q shuriken, optionally saved for after R cast
        if (save_q_for_r && r->is_ready())
        {
            if (q->is_ready() && target->is_valid_target(q_hold_range))
                q->cast(target);
        }
        else
        {
            if (q->is_ready() && target->is_valid_target(q->range()))
                q->cast(target);
        }

        // 5) W2 (swap) cast only if toggle ON and shadow deployed
        if (w_toggle_on && w->is_ready() && shadow_deployed && w->name().find("w2") != std::string::npos)
        {
            bool in_danger = myhero->get_health_percent() < 20 || myhero->count_enemies_in_range(800) > 2;
            if (in_danger || now - last_combo_time > swap_back_delay)
            {
                w->cast();  // W2 cast
                can_swap_back = false;
            }
            else
            {
                can_swap_back = true;
            }
        }
    }

    // --- Harass Logic: Only Q to avoid crashes ---
    void harass_logic(game_object_script target)
    {
        if (harass_min_mana && myhero->get_mana_percent() < harass_min_mana->get_int())
            return;

        if (harass_use_q && harass_use_q->get_bool() && q->is_ready() && target->is_valid_target(q->range()))
            q->cast(target);
    }

    // --- Killsteal logic ---
    void killsteal_logic()
    {
        for (auto& target : entitylist->get_enemy_heroes())
        {
            if (!target || !target->is_valid() || target->is_dead() || !target->is_visible())
                continue;

            if (killsteal_q && killsteal_q->get_bool() && q->is_ready() && target->is_valid_target(q->range()))
            {
                if (get_q_damage(target) > target->get_health())
                {
                    q->cast(target);
                    continue;
                }
            }

            if (killsteal_e && killsteal_e->get_bool() && e->is_ready() && target->is_valid_target(e->range()))
            {
                if (get_e_damage(target) > target->get_health())
                {
                    e->cast();
                    continue;
                }
            }

            if (killsteal_r && killsteal_r->get_bool() && r->is_ready() && target->is_valid_target(r->range()))
            {
                if (!is_overkill(target) && get_r_damage(target) > target->get_health())
                {
                    r->cast(target);
                    continue;
                }
            }
        }
    }

    // --- Farming Logic for lane and jungle ---
    void farm_logic()
    {
        if (spellfarm_key && spellfarm_key->get_bool() && (orbwalker->lane_clear_mode() || orbwalker->last_hit_mode()))
        {
            // Lane minions
            for (auto& minion : entitylist->get_enemy_minions())
            {
                if (!minion || !minion->is_valid() || minion->is_dead())
                    continue;

                if (farm_use_q && farm_use_q->get_bool() && q->is_ready() && minion->is_valid_target(q->range()))
                    q->cast(minion);

                if (farm_use_w && farm_use_w->get_bool() && w->is_ready() && minion->is_valid_target(w->range()))
                    w->cast(minion->get_position());

                if (farm_use_e && farm_use_e->get_bool() && e->is_ready() && minion->is_valid_target(e->range()))
                    e->cast();
            }
        }

        if (spellfarm_key && spellfarm_key->get_bool() && (orbwalker->lane_clear_mode() || orbwalker->last_hit_mode()))
        {
            // Jungle mobs
            for (auto& mob : entitylist->get_jugnle_mobs_minions())
            {
                if (!mob || !mob->is_valid() || mob->is_dead())
                    continue;

                if (jungle_use_q && jungle_use_q->get_bool() && q->is_ready() && mob->is_valid_target(q->range()))
                    q->cast(mob);

                if (jungle_use_w && jungle_use_w->get_bool() && w->is_ready() && mob->is_valid_target(w->range()))
                    w->cast(mob->get_position());

                if (jungle_use_e && jungle_use_e->get_bool() && e->is_ready() && mob->is_valid_target(e->range()))
                    e->cast();
            }
        }
    }

    // --- Drawing Spell Ranges ---
    void on_draw()
    {
        if (!myhero) return;

        if (draw_q_range && draw_q_range->get_bool() && q)
            draw_manager->add_circle(myhero->get_position(), q->range(), D3DCOLOR_ARGB(170, 70, 180, 255));

        if (draw_w_range && draw_w_range->get_bool() && w)
            draw_manager->add_circle(myhero->get_position(), w->range(), D3DCOLOR_ARGB(170, 90, 160, 200));

        if (draw_e_range && draw_e_range->get_bool() && e)
            draw_manager->add_circle(myhero->get_position(), e->range(), D3DCOLOR_ARGB(170, 60, 200, 100));

        if (draw_r_range && draw_r_range->get_bool() && r)
            draw_manager->add_circle(myhero->get_position(), r->range(), D3DCOLOR_ARGB(170, 200, 20, 40));
    }

    // --- Main update loop, called every frame ---
    void on_update()
    {
        if (myhero->is_dead()) return;

        // Update W2 toggle from permashow menu
        if (w2_auto_cast_toggle)
            allow_w2_cast = w2_auto_cast_toggle->get_bool();

        // Execute killsteal checks
        killsteal_logic();

        // Get target in range
        auto target = target_selector->get_target(1100, damage_type::physical);

        // Combo logic activation
        if (allin_key && allin_key->get_bool() && target)
        {
            combo_simple_allin(target);
        }

        // Harass logic activation
        if (harass_key && harass_key->get_bool() && target)
        {
            harass_logic(target);
        }

        // Farming logic
        farm_logic();
    }

    // --- Load plugin: registers spells, menu, callbacks ---
    void load()
    {
        // Register spells with their ranges
        q = plugin_sdk->register_spell(spellslot::q, 900);
        w = plugin_sdk->register_spell(spellslot::w, 700);
        e = plugin_sdk->register_spell(spellslot::e, 300);
        r = plugin_sdk->register_spell(spellslot::r, 625);

        // Setup Q as a skillshot
        q->set_skillshot(0.25f, 50.f, 1700.f,
            { collisionable_objects::heroes, collisionable_objects::yasuo_wall, collisionable_objects::minions },
            skillshot_type::skillshot_line);

        // Register flash if player has it
        auto s1 = myhero->get_spell(spellslot::summoner1)->get_spell_data()->get_name_hash();
        auto s2 = myhero->get_spell(spellslot::summoner2)->get_spell_data()->get_name_hash();
        if (s1 == spell_hash("SummonerFlash"))
            flash = plugin_sdk->register_spell(spellslot::summoner1, 400.f);
        else if (s2 == spell_hash("SummonerFlash"))
            flash = plugin_sdk->register_spell(spellslot::summoner2, 400.f);

        // Create main menu tab
        main_tab = menu->create_tab("zed", "Luvvy Zed");

        // Combo tab and options
        auto combo_tab = main_tab->add_tab(".combo", "Combo");

        allin_key = combo_tab->add_hotkey(".allin", "All-in (combo)", TreeHotkeyMode::Hold, 0x20, true); // Space bar

        combo_save_q_for_r = combo_tab->add_checkbox(".save_q_for_r", "Save Q for after R (higher accuracy)", true);
        combo_q_hold_range = combo_tab->add_slider(".q_hold_range", "Q max range (hold for after R)", 400, 100, 900);
        combo_swap_back_delay = combo_tab->add_slider(".swap_back_delay", "W2/R2 Swapback Delay (ticks 0.1s)", 7, 1, 20);

        manual_w_cooldown = combo_tab->add_slider(".manual_w_cooldown", "Manual W Cooldown (seconds) if Auto W OFF", 5, 1, 20);

        // W2 auto cast toggle key
        w2_auto_cast_toggle = combo_tab->add_hotkey(".w2_auto_cast_toggle", "Toggle W2 Auto Cast", TreeHotkeyMode::Toggle, 'T', true);

        // Harass tab and options
        auto harass_tab = main_tab->add_tab(".harass", "Harass");
        harass_key = harass_tab->add_hotkey(".harass", "Harass", TreeHotkeyMode::Hold, 0x58, false); // X key
        harass_use_q = harass_tab->add_checkbox(".use_q", "Use Q in Harass", true);
        harass_min_mana = harass_tab->add_slider(".min_mana", "Only harass if Mana% above", 30, 0, 100);

        // Farming tab and options
        auto farm_tab = main_tab->add_tab(".farm", "Farming");
        spellfarm_key = farm_tab->add_hotkey(".spellfarm", "Spellfarm (Q/W/E toggle, MB3)", TreeHotkeyMode::Toggle, 0x04, true);
        farm_use_q = farm_tab->add_checkbox(".use_q", "Use Q in Lane Clear", true);
        farm_use_w = farm_tab->add_checkbox(".use_w", "Use W in Lane Clear", false);
        farm_use_e = farm_tab->add_checkbox(".use_e", "Use E in Lane Clear", false);
        jungle_use_q = farm_tab->add_checkbox(".use_q_jungle", "Use Q in Jungle Clear", true);
        jungle_use_w = farm_tab->add_checkbox(".use_w_jungle", "Use W in Jungle Clear", false);
        jungle_use_e = farm_tab->add_checkbox(".use_e_jungle", "Use E in Jungle Clear", false);

        // Killsteal tab and options
        auto ks_tab = main_tab->add_tab(".ks", "Killsteal");
        killsteal_q = ks_tab->add_checkbox(".killsteal.q", "Killsteal Q", true);
        killsteal_e = ks_tab->add_checkbox(".killsteal.e", "Killsteal E", true);
        killsteal_r = ks_tab->add_checkbox(".killsteal.r", "Killsteal R", false);

        // R blacklist menu - disable R on specific champs
        auto rlogic_tab = main_tab->add_tab(".rlogic", "R Target");
        r_blacklist_entries.clear();
        for (auto& enemy : entitylist->get_enemy_heroes())
        {
            if (!enemy || !enemy->is_valid() || enemy->is_dead())
                continue;

            std::string champ_name = enemy->get_model();
            std::transform(champ_name.begin(), champ_name.end(), champ_name.begin(), ::tolower);

            TreeEntry* chk = rlogic_tab->add_checkbox(".rblacklist." + champ_name, champ_name + " Use R", true);
            r_blacklist_entries[champ_name] = chk;
        }

        // Draw tab menu
        auto draw_tab = main_tab->add_tab(".draw", "Draw");
        draw_q_range = draw_tab->add_checkbox(".draw_q_range", "Draw Q Range", true);
        draw_w_range = draw_tab->add_checkbox(".draw_w_range", "Draw W Range", false);
        draw_e_range = draw_tab->add_checkbox(".draw_e_range", "Draw E Range", false);
        draw_r_range = draw_tab->add_checkbox(".draw_r_range", "Draw R Range", true);

        // Add to permashow overlay
        Permashow::Instance.Init(main_tab);
        Permashow::Instance.AddElement("All-in", allin_key);
        Permashow::Instance.AddElement("Harass", harass_key);
        Permashow::Instance.AddElement("Spellfarm", spellfarm_key);
        Permashow::Instance.AddElement("W2 Auto Cast", w2_auto_cast_toggle);

        // Register event callbacks
        event_handler<events::on_update>::add_callback(on_update);
        event_handler<events::on_draw>::add_callback(on_draw);
        event_handler<events::on_process_spell_cast>::add_callback(on_process_spell_cast);
    }

    // --- Unload plugin clean up ---
    void unload()
    {
        event_handler<events::on_update>::remove_handler(on_update);
        event_handler<events::on_draw>::remove_handler(on_draw);
        event_handler<events::on_process_spell_cast>::remove_handler(on_process_spell_cast);
        Permashow::Instance.Destroy();
        menu->delete_tab(main_tab);
    }
}
