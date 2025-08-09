#include "../plugin_sdk/plugin_sdk.hpp"
#include "kalista.h"
#include "kennen.h"
#include "zilean.h"
#include "nidalee.h"
#include "shyvana.h"
#include "zed.h"
#include "taliyah.h"
#include "yunara.h"
#include "utils.h"
#include "permashow.hpp"

PLUGIN_NAME("Luvvy AIO ");
PLUGIN_TYPE(plugin_type::misc);  // <- Ensures plugin appears under the 'Misc' tab

// Function pointers to the currently loaded champion logic
namespace {
    void (*champion_load)() = nullptr;
    void (*champion_unload)() = nullptr;
}

extern "C" PLUGIN_API bool on_sdk_load(plugin_sdk_core* sdk_core)
{
    DECLARE_GLOBALS(sdk_core);

    std::string champion = myhero->get_model();

    // Detect and assign champion-specific logic
    if (champion == "Kalista") {
        champion_load = kalista::load;
        champion_unload = kalista::unload;
    } else if (champion == "Yunara") {
        champion_load = yunara::load;
        champion_unload = yunara::unload;
    } else if (champion == "Taliyah") {
        champion_load = taliyah::load;
        champion_unload = taliyah::unload;
    } else if (champion == "Shyvana") {
        champion_load = shyvana::load;
        champion_unload = shyvana::unload;
    } else if (champion == "Zed") {
        champion_load = zed::load;
        champion_unload = zed::unload;
    } else if (champion == "Kennen") {
        champion_load = kennen::load;
        champion_unload = kennen::unload;
    } else if (champion == "Nidalee") {
        champion_load = nidalee::load;
        champion_unload = nidalee::unload;
    } else if (champion == "Zilean") {
        champion_load = zilean::load;
        champion_unload = zilean::unload;
    } else {
        console->print("This champion is not supported by the AIO.");
        return false;
    }

    // Initialize permashow or any shared UI if needed
    // Example (optional): permashow::setup();

    // Call the selected champion's load logic
    if (champion_load) champion_load();
    return true;
}

extern "C" PLUGIN_API void on_sdk_unload()
{
    // Unload the currently loaded champion's logic, if any
    if (champion_unload) champion_unload();
}
